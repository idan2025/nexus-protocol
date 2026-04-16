/*
 * NEXUS Firmware -- RAK4631 (nRF52840 + SX1262)
 *
 * Full NEXUS mesh node with:
 * - SX1262 LoRa radio (via RadioLib HAL bridge)
 * - BLE via nRF52840 built-in (Bluefruit/SoftDevice)
 * - Identity persistence in internal flash
 *
 * The RAK4631 is the most memory-constrained target (256KB SRAM).
 * NEXUS memory budget: ~95KB (node + routes + sessions + fragments)
 */
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

/* NEXUS C API */
extern "C" {
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/lora_radio.h"
}

#include "radiolib_hal.h"
#include "ble_bridge.h"
#include "settings_store.h"
#include "anchor_store.h"
#include "battery.h"
#include "event_ring.h"
#include "session_store.h"

/* ── Pin definitions (RAK4631 WisBlock) ───────────────────────────────── */
/*
 * IMPORTANT: The Adafruit BSP's WB_IO1/2/3 are WisBlock Base slot connector
 * pins, NOT the onboard SX1262 radio pins! The SX1262 on the RAK4631 module
 * has dedicated connections. Pin numbers confirmed by RNode firmware and the
 * RAK4631 schematic.
 */
#define LORA_CS     42   /* P1.10 - SX1262 NSS */
#define LORA_DIO1   47   /* P1.15 - SX1262 DIO1 */
#define LORA_RST    38   /* P1.06 - SX1262 RESET */
#define LORA_BUSY   46   /* P1.14 - SX1262 BUSY */
#define LORA_RXEN   37   /* P1.05 - RF switch RX enable */

/* SX1262 SPI bus (separate from WisBlock IO slot default SPI on pins 3/29/30) */
#define LORA_SCK    43   /* P1.11 */
#define LORA_MISO   45   /* P1.13 */
#define LORA_MOSI   44   /* P1.12 */

/* TCXO voltage for RAK4631's SX1262 (DIO3-controlled) */
#define LORA_TCXO_VOLTAGE 1.6f

/* LED_GREEN and LED_BLUE are defined in variant.h */

/* ── Globals ──────────────────────────────────────────────────────────── */

/*
 * IMPORTANT: Do NOT create RadioLib Module as a global constructor!
 * On nRF52840 (ARM Cortex-M4), global constructors run before the Arduino
 * framework initializes SPI/GPIO. The Module() constructor accesses hardware
 * pins, causing a hard fault before setup() is even reached.
 * We use a pointer and create it in setup() after hardware is initialized.
 */
static SX1262 *radio_ptr = NULL;

static nx_node_t node;
static nx_settings_t settings;
static nx_lora_radio_t *g_lora_radio = NULL;
static char ble_name[20];
static uint32_t msg_count = 0;
static uint32_t neighbor_count = 0;
static int last_anchor_count = 0;
static int last_session_count = 0;

/* ── Flash identity storage (nRF52 InternalFS) ────────────────────────── */

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;
static const char *ID_FILE = "/nexus_id";

static nx_err_t load_identity(nx_identity_t *id)
{
    InternalFS.begin();
    File id_file(InternalFS);
    id_file.open(ID_FILE, FILE_O_READ);
    if (!id_file) return NX_ERR_NOT_FOUND;

    size_t read = id_file.read(id, sizeof(nx_identity_t));
    id_file.close();

    if (read != sizeof(nx_identity_t)) return NX_ERR_NOT_FOUND;

    /* Verify not all zeros */
    uint8_t zeros[32] = {0};
    if (memcmp(id->sign_public, zeros, 32) == 0) return NX_ERR_NOT_FOUND;

    return NX_OK;
}

static nx_err_t save_identity(const nx_identity_t *id)
{
    InternalFS.begin();
    if (InternalFS.exists(ID_FILE)) InternalFS.remove(ID_FILE);

    File id_file(InternalFS);
    id_file.open(ID_FILE, FILE_O_WRITE);
    if (!id_file) return NX_ERR_IO;

    size_t written = id_file.write((const uint8_t *)id, sizeof(nx_identity_t));
    id_file.close();

    return (written == sizeof(nx_identity_t)) ? NX_OK : NX_ERR_IO;
}

/* ── Callbacks ────────────────────────────────────────────────────────── */

static void on_data(const nx_addr_short_t *src,
                    const uint8_t *data, size_t len, void *user)
{
    (void)user;
    msg_count++;
    Serial.printf("[RX] From %02X%02X%02X%02X len=%d\n",
                  src->bytes[0], src->bytes[1],
                  src->bytes[2], src->bytes[3], (int)len);

    /* Forward to phone via BLE bridge */
    if (nx_ble_bridge_connected()) {
        uint8_t frame[NX_MAX_PACKET];
        if (len + 6 <= sizeof(frame)) {
            memcpy(frame, src->bytes, 4);
            frame[4] = (uint8_t)(len >> 8);
            frame[5] = (uint8_t)(len & 0xFF);
            memcpy(&frame[6], data, len);
            nx_ble_bridge_send(frame, len + 6);
        }
    }
}

static void on_neighbor(const nx_addr_short_t *addr,
                        nx_role_t role, void *user)
{
    (void)user;
    neighbor_count++;
    Serial.printf("[NBR] %02X%02X%02X%02X role=%d\n",
                  addr->bytes[0], addr->bytes[1],
                  addr->bytes[2], addr->bytes[3], role);
}

/* ── Setup ────────────────────────────────────────────────────────────── */

void setup()
{
    /* LEDs first -- earliest visual indicator that code is running */
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_GREEN, LOW);  /* GREEN on = boot started */
    digitalWrite(LED_BLUE, LOW);

    Serial.begin(115200);
    delay(2000); /* nRF52 USB serial needs extra time */
    Serial.println("\n[NEXUS] RAK4631 starting...");

    /* Boot stage: BLUE blinks = initializing */
    digitalWrite(LED_GREEN, HIGH);  /* GREEN off */
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_BLUE, LOW);   /* on */
        delay(100);
        digitalWrite(LED_BLUE, HIGH);  /* off */
        delay(100);
    }

    /* Load settings (or use defaults on first boot) */
    if (nx_settings_load(&settings) != NX_OK) {
        Serial.println("[NEXUS] First boot - using default settings");
        nx_settings_t defaults = NX_SETTINGS_DEFAULT;
        memcpy(&settings, &defaults, sizeof(settings));
        nx_settings_save(&settings);
    } else {
        Serial.printf("[NEXUS] Settings loaded: freq=%lu sf=%d role=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      settings.lora_config.spreading_factor,
                      settings.node_role);
    }

    /* ── Identity (BEFORE BLE so we have a name) ─────────────────────── */

    nx_identity_t stored_id;
    bool new_identity = false;
    if (load_identity(&stored_id) != NX_OK) {
        Serial.println("[NEXUS] Generating new identity...");
        nx_identity_generate(&stored_id);
        save_identity(&stored_id);
        new_identity = true;
    } else {
        Serial.println("[NEXUS] Identity loaded from flash");
    }

    snprintf(ble_name, sizeof(ble_name), "NEXUS-%02X%02X",
             stored_id.short_addr.bytes[0], stored_id.short_addr.bytes[1]);

    Serial.printf("[NEXUS] Identity: %02X%02X%02X%02X %s\n",
                  stored_id.short_addr.bytes[0], stored_id.short_addr.bytes[1],
                  stored_id.short_addr.bytes[2], stored_id.short_addr.bytes[3],
                  new_identity ? "(new)" : "(stored)");

    /* ── BLE bridge (EARLY -- always discoverable even if LoRa fails) ── */

    nx_ble_bridge_init(ble_name);
    nx_ble_bridge_start();
    Serial.printf("[NEXUS] BLE advertising: %s\n", ble_name);

    battery_init();
    nx_event_log("boot");

    /* ── Node init ───────────────────────────────────────────────────── */

    nx_node_config_t cfg = {
        .role              = (nx_role_t)settings.node_role,
        .default_ttl       = 7,
        .beacon_interval_ms = 30000,
        .on_data           = on_data,
        .on_neighbor       = on_neighbor,
        .on_session        = NULL,
        .on_group          = NULL,
        .user_ctx          = NULL,
    };

    if (nx_node_init_with_identity(&node, &cfg, &stored_id) != NX_OK) {
        Serial.println("[NEXUS] Node init failed!");
        /* Continue anyway -- BLE is already running */
    }

    /* ── LoRa radio (non-fatal -- BLE works without it) ──────────────── */

    /*
     * Create RadioLib Module HERE, after Arduino framework has initialized
     * SPI and GPIO. Creating it as a global causes hard fault on nRF52840!
     *
     * RAK4631's onboard SX1262 uses a SEPARATE SPI bus (pins 43/44/45)
     * from the default WisBlock IO slot SPI (pins 3/29/30). We must create
     * a custom SPIClass instance for the correct pins.
     */
    static SPIClass SPI_LORA(NRF_SPIM3, LORA_MISO, LORA_SCK, LORA_MOSI);
    SPI_LORA.begin();

    static Module lora_module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, SPI_LORA);
    static SX1262 radio_instance(&lora_module);
    radio_ptr = &radio_instance;
    Serial.println("[NEXUS] RadioLib module created (deferred init, dedicated SPI)");

    nx_transport_registry_init();

    /* Set TCXO voltage -- RAK4631 has TCXO controlled via SX1262 DIO3 */
    settings.lora_config.tcxo_voltage = LORA_TCXO_VOLTAGE;

    bool lora_ok = false;
    g_lora_radio = nx_radiolib_create(radio_ptr);
    if (!g_lora_radio || g_lora_radio->ops->init(g_lora_radio, &settings.lora_config) != NX_OK) {
        Serial.println("[NEXUS] LoRa radio init FAILED -- check SX1262 WisBlock module");
        Serial.println("[NEXUS] BLE bridge is still active");
    } else {
        /* Configure RF switch: DIO2 handles TX, RXEN GPIO handles RX */
        static const uint32_t rfswitch_pins[] = {
            LORA_RXEN, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
        };
        static const Module::RfSwitchMode_t rfswitch_table[] = {
            {Module::MODE_IDLE, {LOW}},
            {Module::MODE_RX,   {HIGH}},
            {Module::MODE_TX,   {LOW}},
            END_OF_MODE_TABLE,
        };
        radio_ptr->setRfSwitchTable(rfswitch_pins, rfswitch_table);

        nx_transport_t *lora_t = nx_lora_transport_create();
        if (lora_t && lora_t->ops->init(lora_t, &g_lora_radio) == NX_OK &&
            nx_transport_register(lora_t) == NX_OK) {
            lora_ok = true;
            Serial.println("[NEXUS] LoRa transport OK");
        } else {
            Serial.println("[NEXUS] LoRa transport setup failed");
        }
    }

    /* Load stored messages from flash */
    if (nx_anchor_store_load(&node.anchor) == NX_OK) {
        int count = nx_anchor_count(&node.anchor);
        Serial.printf("[NEXUS] Loaded %d stored messages from flash\n", count);
        last_anchor_count = count;
    }

    /* Resume Double Ratchet sessions from flash */
    if (nx_session_store_load(&node.sessions) == NX_OK) {
        last_session_count = nx_session_count(&node.sessions);
        Serial.printf("[NEXUS] Resumed %d sessions from flash\n",
                      last_session_count);
    }

    /* Initial announcement (only if LoRa is up) */
    if (lora_ok) {
        nx_node_announce(&node);
    }

    /* Boot result LED */
    if (lora_ok) {
        digitalWrite(LED_GREEN, LOW);
        delay(2000);
        digitalWrite(LED_GREEN, HIGH);
    } else {
        /* BLE only: rapid GREEN+BLUE alternating */
        for (int i = 0; i < 5; i++) {
            digitalWrite(LED_GREEN, LOW);
            delay(300);
            digitalWrite(LED_GREEN, HIGH);
            digitalWrite(LED_BLUE, LOW);
            delay(300);
            digitalWrite(LED_BLUE, HIGH);
        }
    }

    Serial.printf("[NEXUS] Ready -- BLE: %s, LoRa: %s\n",
                  ble_name, lora_ok ? "OK" : "FAILED");
}

/* ── Main Loop ────────────────────────────────────────────────────────── */

static uint32_t last_status_ms = 0;

void loop()
{
    nx_node_poll(&node, 10);

    /* Check for packets from phone via BLE */
    if (nx_ble_bridge_connected()) {
        uint8_t ble_buf[NX_MAX_PACKET];
        size_t ble_len = 0;

        while (nx_ble_bridge_recv(ble_buf, sizeof(ble_buf), &ble_len) == NX_OK) {
            if (ble_len > 4) {
                nx_addr_short_t dest;
                memcpy(dest.bytes, ble_buf, 4);
                nx_node_send(&node, &dest, &ble_buf[4], ble_len - 4);
            }
        }
    }

    nx_ble_bridge_poll();

    /* Persist anchor mailbox when contents change */
    int cur_anchor = nx_anchor_count(&node.anchor);
    if (cur_anchor != last_anchor_count) {
        nx_anchor_store_save(&node.anchor);
        Serial.printf("[ANCHOR] Saved %d messages to flash\n", cur_anchor);
        last_anchor_count = cur_anchor;
    }

    /* Persist session store when a new handshake lands. Ratchet keys
     * evolve on every message; saving per-message burns flash too fast,
     * so we only snapshot on count change — good enough for resume. */
    int cur_sessions = nx_session_count(&node.sessions);
    if (cur_sessions != last_session_count) {
        nx_session_store_save(&node.sessions);
        Serial.printf("[SESSION] Saved %d sessions to flash\n", cur_sessions);
        last_session_count = cur_sessions;
    }

    uint32_t now = millis();

    /* Heartbeat blink every 5s */
    static uint32_t last_heartbeat_ms = 0;
    if (now - last_heartbeat_ms > 5000) {
        last_heartbeat_ms = now;
        digitalWrite(LED_GREEN, LOW);
        delay(50);
        digitalWrite(LED_GREEN, HIGH);
    }

    /* Periodic serial status every 30s */
    if (now - last_status_ms > 30000) {
        last_status_ms = now;

        /* Attach battery readings to outbound announces */
        int32_t bat_mv = battery_read_mv();
        int bat_pct = battery_percent();
        if (bat_mv > 0) {
            nx_announce_telemetry_t telem = {0};
            telem.battery_mv  = (uint16_t)bat_mv;
            telem.battery_pct = (uint8_t)bat_pct;
            nx_node_set_telemetry(&node, &telem);
        }

        const nx_identity_t *id = nx_node_identity(&node);
        Serial.printf("[STATUS] %02X%02X%02X%02X nbrs=%lu msgs=%lu stored=%d BLE=%s bat=%ldmV(%d%%)\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      (unsigned long)neighbor_count,
                      (unsigned long)msg_count,
                      nx_anchor_count(&node.anchor),
                      nx_ble_bridge_connected() ? "yes" : "no",
                      (long)bat_mv, bat_pct);
    }
}
