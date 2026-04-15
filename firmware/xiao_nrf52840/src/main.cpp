/*
 * NEXUS Firmware -- Seeed XIAO nRF52840 + WIO-SX1262
 *
 * Headless NEXUS mesh node with:
 * - SX1262 LoRa radio (via RadioLib HAL bridge)
 * - BLE bridge for phone app connectivity (Bluefruit NUS)
 * - BLE config protocol (settings over BLE)
 * - Identity persistence in internal flash (LittleFS)
 * - Store-and-forward persistence in flash
 * - No display -- status via serial + LED + BLE
 *
 * Memory budget: ~95KB (node + routes + sessions + fragments)
 * The nRF52840 has 256KB SRAM, leaving ~160KB for stack/heap/BLE.
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
#include "nexus/route.h"
#include "nexus/anchor.h"
}

#include "radiolib_hal.h"
#include "ble_bridge.h"
#include "settings_store.h"
#include "anchor_store.h"
#include "battery.h"
#include "event_ring.h"

/* -- Pin definitions (XIAO nRF52840 + WIO-SX1262 expansion) -------------- */
/*
 * WIO-SX1262 for XIAO (standalone, SKU 113010003 / nRF52840 kit SKU 102010710)
 * Pin mapping confirmed by Meshtastic firmware and Seeed schematic:
 *   https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf
 *
 *   D1  -> SX1262 DIO1
 *   D2  -> SX1262 RESET
 *   D3  -> SX1262 BUSY
 *   D4  -> SX1262 NSS (chip select)
 *   D5  -> SX1262 RXEN (RF switch RX enable)
 *   D8  -> SPI SCK   (default SPI bus)
 *   D9  -> SPI MISO  (default SPI bus)
 *   D10 -> SPI MOSI  (default SPI bus)
 */
#define LORA_SS     4   /* D4 */
#define LORA_DIO1   1   /* D1 */
#define LORA_RST    2   /* D2 */
#define LORA_BUSY   3   /* D3 */
#define LORA_RXEN   5   /* D5 - RF switch RX enable */

/* TCXO voltage for WIO-SX1262 (DIO3-controlled) */
#define LORA_TCXO_VOLTAGE 1.8f

/* XIAO nRF52840 has LEDs defined in variant:
 * LED_RED (11), LED_GREEN (not available on base model)
 * LED_BUILTIN = LED_RED on XIAO nRF52840 */
#ifndef LED_BUILTIN
#define LED_BUILTIN 11
#endif

/* RGB LEDs on XIAO nRF52840 Sense (active LOW) */
#ifndef LED_RED
#define LED_RED    11
#endif
#ifndef LED_GREEN
#define LED_GREEN  13
#endif
#ifndef LED_BLUE
#define LED_BLUE   12
#endif

/* -- Flash identity storage (nRF52 InternalFS) --------------------------- */

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

/* -- Globals ------------------------------------------------------------- */

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

/* Stats */
static uint32_t rx_count = 0;
static uint32_t neighbor_count = 0;
static int last_anchor_count = 0;

/* LED blink state */
static uint32_t led_off_ms = 0;

/* -- BLE Config Protocol ------------------------------------------------- */

static const uint8_t CFG_MAGIC[4] = {0xFF, 0xFF, 0xFF, 0xCF};

#define CFG_CMD_GET_CONFIG   0x01
#define CFG_CMD_SET_RADIO    0x02
#define CFG_CMD_SET_SCREEN   0x03
#define CFG_CMD_SET_ROLE     0x04
#define CFG_CMD_REBOOT       0x05

#define CFG_RESP_FLAG        0x80

/* -- Helpers ------------------------------------------------------------- */

static const char* role_name(int role)
{
    switch (role) {
    case 0: return "LEAF";
    case 1: return "RELAY";
    case 2: return "GATEWAY";
    case 3: return "ANCHOR";
    case 4: return "SENTINL";
    case 5: return "PILLAR";
    case 6: return "VAULT";
    default: return "???";
    }
}

static void led_blink(uint32_t duration_ms)
{
    digitalWrite(LED_BUILTIN, LOW);  /* active LOW on XIAO */
    led_off_ms = millis() + duration_ms;
}

static void led_double_blink()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(60);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(60);
    digitalWrite(LED_BUILTIN, LOW);
    led_off_ms = millis() + 60;
}

/* -- Callbacks ----------------------------------------------------------- */

static void on_data(const nx_addr_short_t *src,
                    const uint8_t *data, size_t len, void *user)
{
    (void)user;
    rx_count++;
    Serial.printf("[RX] From %02X%02X%02X%02X len=%d\n",
                  src->bytes[0], src->bytes[1],
                  src->bytes[2], src->bytes[3], (int)len);

    led_double_blink();

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

static void on_session(const nx_addr_short_t *src,
                       const uint8_t *data, size_t len, void *user)
{
    (void)user;
    rx_count++;
    Serial.printf("[SESSION] From %02X%02X%02X%02X len=%d\n",
                  src->bytes[0], src->bytes[1],
                  src->bytes[2], src->bytes[3], (int)len);

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

/* -- BLE Config Protocol Handler ----------------------------------------- */

static void send_config_response()
{
    uint8_t resp[25];
    memcpy(resp, CFG_MAGIC, 4);
    resp[4] = CFG_CMD_GET_CONFIG | CFG_RESP_FLAG;

    const nx_lora_config_t *lc = &settings.lora_config;
    resp[5]  = (uint8_t)(lc->frequency_hz);
    resp[6]  = (uint8_t)(lc->frequency_hz >> 8);
    resp[7]  = (uint8_t)(lc->frequency_hz >> 16);
    resp[8]  = (uint8_t)(lc->frequency_hz >> 24);
    resp[9]  = (uint8_t)(lc->bandwidth_hz);
    resp[10] = (uint8_t)(lc->bandwidth_hz >> 8);
    resp[11] = (uint8_t)(lc->bandwidth_hz >> 16);
    resp[12] = (uint8_t)(lc->bandwidth_hz >> 24);
    resp[13] = lc->spreading_factor;
    resp[14] = lc->coding_rate;
    resp[15] = (uint8_t)lc->tx_power_dbm;
    resp[16] = 0; /* screen_timeout N/A */
    resp[17] = 0;
    resp[18] = 0;
    resp[19] = 0;
    resp[20] = settings.node_role;

    const nx_identity_t *id = nx_node_identity(&node);
    memcpy(&resp[21], id->short_addr.bytes, 4);

    nx_ble_bridge_send(resp, sizeof(resp));
}

static void handle_ble_config(const uint8_t *payload, size_t len)
{
    if (len < 1) return;
    uint8_t cmd = payload[0];

    switch (cmd) {
    case CFG_CMD_GET_CONFIG:
        Serial.println("[CFG] GET_CONFIG");
        send_config_response();
        break;

    case CFG_CMD_SET_RADIO:
        if (len < 12) break;
        {
            uint32_t freq = (uint32_t)payload[1] |
                            ((uint32_t)payload[2] << 8) |
                            ((uint32_t)payload[3] << 16) |
                            ((uint32_t)payload[4] << 24);
            uint32_t bw   = (uint32_t)payload[5] |
                            ((uint32_t)payload[6] << 8) |
                            ((uint32_t)payload[7] << 16) |
                            ((uint32_t)payload[8] << 24);
            uint8_t sf    = payload[9];
            uint8_t cr    = payload[10];
            int8_t pwr    = (int8_t)payload[11];

            if (freq < 137000000 || freq > 1020000000) break;
            if (sf < 7 || sf > 12) break;
            if (cr < 5 || cr > 8) break;
            if (pwr < 2 || pwr > 22) break;

            settings.lora_config.frequency_hz = freq;
            settings.lora_config.bandwidth_hz = bw;
            settings.lora_config.spreading_factor = sf;
            settings.lora_config.coding_rate = cr;
            settings.lora_config.tx_power_dbm = pwr;

            if (g_lora_radio && g_lora_radio->ops->reconfigure) {
                nx_err_t err = g_lora_radio->ops->reconfigure(
                    g_lora_radio, &settings.lora_config);
                Serial.printf("[CFG] SET_RADIO freq=%lu bw=%lu sf=%d cr=%d pwr=%d -> %s\n",
                              (unsigned long)freq, (unsigned long)bw,
                              sf, cr, pwr,
                              err == NX_OK ? "OK" : "FAIL");
            }

            nx_settings_save(&settings);
            send_config_response();
        }
        break;

    case CFG_CMD_SET_SCREEN:
        Serial.println("[CFG] SET_SCREEN (ignored, headless node)");
        send_config_response();
        break;

    case CFG_CMD_SET_ROLE:
        if (len < 2) break;
        {
            uint8_t role = payload[1];
            if (role > 6) break;
            settings.node_role = role;
            nx_settings_save(&settings);
            Serial.printf("[CFG] SET_ROLE role=%d (%s)\n",
                          role, role_name(role));
            send_config_response();
        }
        break;

    case CFG_CMD_REBOOT:
        Serial.println("[CFG] REBOOT");
        nx_anchor_store_save(&node.anchor);
        nx_settings_save(&settings);
        delay(500);
        NVIC_SystemReset();
        break;

    default:
        Serial.printf("[CFG] Unknown cmd 0x%02X\n", cmd);
        break;
    }
}

/* -- Serial commands ----------------------------------------------------- */

static void process_serial_command(const char *line)
{
    if (strcmp(line, "STATUS") == 0) {
        const nx_identity_t *id = nx_node_identity(&node);
        Serial.printf("[STATUS] %02X%02X%02X%02X nbrs=%lu msgs=%lu stored=%d BLE=%s\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      (unsigned long)neighbor_count,
                      (unsigned long)rx_count,
                      nx_anchor_count(&node.anchor),
                      nx_ble_bridge_connected() ? "yes" : "no");
    } else if (strcmp(line, "ANNOUNCE") == 0) {
        nx_node_announce(&node);
        Serial.println("[ANNOUNCE] Sent");
    } else if (strcmp(line, "NEIGHBORS") == 0) {
        int count = nx_neighbor_count(&node.route_table);
        Serial.printf("[NEIGHBORS] %d total\n", count);
        for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
            const nx_neighbor_t *n = &node.route_table.neighbors[i];
            if (!n->valid) continue;
            Serial.printf("  %02X%02X%02X%02X  %s\n",
                          n->addr.bytes[0], n->addr.bytes[1],
                          n->addr.bytes[2], n->addr.bytes[3],
                          role_name(n->role));
        }
    } else if (strcmp(line, "MAILBOX") == 0) {
        int count = nx_anchor_count(&node.anchor);
        Serial.printf("[MAILBOX] %d/%d slots used, TTL=%lus\n",
                      count, node.anchor.max_slots,
                      (unsigned long)(node.anchor.msg_ttl_ms / 1000));
    } else if (strcmp(line, "RADIO") == 0) {
        Serial.printf("[RADIO] freq=%lu bw=%lu sf=%d cr=%d pwr=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      (unsigned long)settings.lora_config.bandwidth_hz,
                      settings.lora_config.spreading_factor,
                      settings.lora_config.coding_rate,
                      settings.lora_config.tx_power_dbm);
    } else if (strcmp(line, "SETTINGS") == 0) {
        Serial.printf("[SETTINGS] role=%d(%s)\n",
                      settings.node_role, role_name(settings.node_role));
        Serial.printf("[SETTINGS] freq=%lu bw=%lu sf=%d cr=%d pwr=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      (unsigned long)settings.lora_config.bandwidth_hz,
                      settings.lora_config.spreading_factor,
                      settings.lora_config.coding_rate,
                      settings.lora_config.tx_power_dbm);
    } else if (strcmp(line, "HELP") == 0) {
        Serial.println("Commands: STATUS ANNOUNCE NEIGHBORS MAILBOX RADIO SETTINGS HELP");
    }
}

/* -- Setup --------------------------------------------------------------- */

void setup()
{
    /*
     * FIRST: LED blink before ANYTHING else.
     * This is the earliest possible visual indicator that code is running.
     * If you see this blink but nothing after, the crash is in init below.
     */
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);

    /* All LEDs off first (active LOW) */
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    /* Boot stage 1: RED = hardware init starting */
    digitalWrite(LED_RED, LOW);  /* RED on */
    delay(500);
    digitalWrite(LED_RED, HIGH); /* RED off */

    Serial.begin(115200);
    delay(2000); /* nRF52 USB serial needs extra time */
    Serial.println("\n[NEXUS] XIAO nRF52840 + WIO-SX1262 starting...");

    /* Boot stage 2: BLUE blinks = initializing */
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BLUE, LOW);   /* on */
        delay(150);
        digitalWrite(LED_BLUE, HIGH);  /* off */
        delay(150);
    }

    /*
     * DIAGNOSTIC STEP INDICATORS
     * Each init step is preceded by N RED blinks (with a long pause after).
     * Count the RED blinks to identify which step crashes:
     *   1 RED blink  = about to load settings (InternalFS)
     *   2 RED blinks = about to load/generate identity
     *   3 RED blinks = about to init BLE (Bluefruit/SoftDevice)
     *   4 RED blinks = about to init NEXUS node
     *   5 RED blinks = about to init LoRa radio (SPI + RadioLib)
     *   6 RED blinks = about to init LoRa transport layer
     *   GREEN solid  = boot complete
     */

    /* ── STEP 1: Settings ──────────────────────────────────────────────── */
    digitalWrite(LED_RED, LOW); delay(400); digitalWrite(LED_RED, HIGH); delay(600);
    Serial.println("[BOOT] Step 1: Loading settings...");

    if (nx_settings_load(&settings) != NX_OK) {
        Serial.println("[NEXUS] First boot - using default settings");
        nx_settings_t defaults = NX_SETTINGS_DEFAULT;
        memcpy(&settings, &defaults, sizeof(settings));
        settings.screen_timeout_ms = 0; /* headless */
        nx_settings_save(&settings);
    } else {
        Serial.printf("[NEXUS] Settings loaded: freq=%lu sf=%d role=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      settings.lora_config.spreading_factor,
                      settings.node_role);
    }

    /* ── STEP 2: Identity ──────────────────────────────────────────────── */
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_RED, LOW); delay(200); digitalWrite(LED_RED, HIGH); delay(200);
    }
    delay(400);
    Serial.println("[BOOT] Step 2: Loading identity...");

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

    /* ── STEP 3: BLE ───────────────────────────────────────────────────── */
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_RED, LOW); delay(200); digitalWrite(LED_RED, HIGH); delay(200);
    }
    delay(400);
    Serial.println("[BOOT] Step 3: Initializing BLE...");

    nx_ble_bridge_init(ble_name);
    nx_ble_bridge_start();
    Serial.printf("[NEXUS] BLE advertising: %s\n", ble_name);

    battery_init();
    nx_event_log("boot");

    /* ── STEP 4: Node init ─────────────────────────────────────────────── */
    for (int i = 0; i < 4; i++) {
        digitalWrite(LED_RED, LOW); delay(200); digitalWrite(LED_RED, HIGH); delay(200);
    }
    delay(400);
    Serial.println("[BOOT] Step 4: Initializing NEXUS node...");

    nx_node_config_t cfg = {
        .role              = (nx_role_t)settings.node_role,
        .default_ttl       = 7,
        .beacon_interval_ms = 30000,
        .on_data           = on_data,
        .on_neighbor       = on_neighbor,
        .on_session        = on_session,
        .on_group          = NULL,
        .user_ctx          = NULL,
    };

    if (nx_node_init_with_identity(&node, &cfg, &stored_id) != NX_OK) {
        Serial.println("[NEXUS] Node init failed!");
        /* Continue anyway -- BLE is already running */
    }

    /* ── STEP 5: LoRa radio ────────────────────────────────────────────── */
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_RED, LOW); delay(200); digitalWrite(LED_RED, HIGH); delay(200);
    }
    delay(400);
    Serial.println("[BOOT] Step 5: Initializing LoRa radio...");

    /*
     * Create RadioLib Module HERE, after Arduino framework has initialized
     * SPI and GPIO. Creating it as a global causes hard fault on nRF52840!
     */
    SPI.begin();  /* Ensure default SPI bus is initialized */
    static Module lora_module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY, SPI);
    static SX1262 radio_instance(&lora_module);
    radio_ptr = &radio_instance;
    Serial.println("[NEXUS] RadioLib module created (deferred init)");

    nx_transport_registry_init();

    /* Set TCXO voltage -- WIO-SX1262 has TCXO controlled via SX1262 DIO3 */
    settings.lora_config.tcxo_voltage = LORA_TCXO_VOLTAGE;

    bool lora_ok = false;
    g_lora_radio = nx_radiolib_create(radio_ptr);
    if (!g_lora_radio || g_lora_radio->ops->init(g_lora_radio, &settings.lora_config) != NX_OK) {
        Serial.println("[NEXUS] LoRa radio init FAILED -- check WIO-SX1262 connection");
        Serial.println("[NEXUS] BLE bridge is still active");
    } else {
        /* ── STEP 6: LoRa transport ────────────────────────────────────── */
        for (int i = 0; i < 6; i++) {
            digitalWrite(LED_RED, LOW); delay(150); digitalWrite(LED_RED, HIGH); delay(150);
        }
        delay(400);
        Serial.println("[BOOT] Step 6: Setting up LoRa transport...");

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

    /* Initial announcement (only if LoRa is up) */
    if (lora_ok) {
        nx_node_announce(&node);
    }

    /* Boot complete: solid GREEN for 2s */
    Serial.printf("[NEXUS] Ready -- BLE: %s, LoRa: %s\n",
                  ble_name, lora_ok ? "OK" : "FAILED");
    Serial.println("[NEXUS] Commands: STATUS ANNOUNCE NEIGHBORS MAILBOX RADIO SETTINGS HELP");

    if (lora_ok) {
        digitalWrite(LED_GREEN, LOW);
        delay(2000);
        digitalWrite(LED_GREEN, HIGH);
    } else {
        /* BLE only (no LoRa): alternating RED+BLUE */
        for (int i = 0; i < 5; i++) {
            digitalWrite(LED_RED, LOW);
            delay(300);
            digitalWrite(LED_RED, HIGH);
            digitalWrite(LED_BLUE, LOW);
            delay(300);
            digitalWrite(LED_BLUE, HIGH);
        }
    }
}

/* -- Main Loop ----------------------------------------------------------- */

static uint32_t last_status_ms = 0;

void loop()
{
    nx_node_poll(&node, 10);

    /* Check for packets from phone via BLE */
    if (nx_ble_bridge_connected()) {
        uint8_t ble_buf[NX_MAX_PACKET];
        size_t ble_len = 0;

        while (nx_ble_bridge_recv(ble_buf, sizeof(ble_buf), &ble_len) == NX_OK) {
            if (ble_len >= 5 && memcmp(ble_buf, CFG_MAGIC, 4) == 0) {
                handle_ble_config(&ble_buf[4], ble_len - 4);
            } else if (ble_len > 4) {
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

    /* LED auto-off after blink */
    if (led_off_ms > 0 && millis() >= led_off_ms) {
        digitalWrite(LED_BUILTIN, HIGH); /* off */
        led_off_ms = 0;
    }

    /* Serial command processing */
    static char serial_buf[128];
    static int serial_pos = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_pos > 0) {
                serial_buf[serial_pos] = '\0';
                process_serial_command(serial_buf);
                serial_pos = 0;
            }
        } else if (serial_pos < (int)sizeof(serial_buf) - 1) {
            serial_buf[serial_pos++] = c;
        }
    }

    /* Heartbeat blink every 5s so user knows it's alive */
    static uint32_t last_heartbeat_ms = 0;
    uint32_t now = millis();
    if (now - last_heartbeat_ms > 5000) {
        last_heartbeat_ms = now;
        led_blink(150);
    }

    /* Periodic serial status every 30s */
    if (now - last_status_ms > 30000) {
        last_status_ms = now;

        const nx_identity_t *id = nx_node_identity(&node);
        Serial.printf("[STATUS] %02X%02X%02X%02X nbrs=%d msgs=%lu stored=%d BLE=%s\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      nx_neighbor_count(&node.route_table),
                      (unsigned long)rx_count,
                      nx_anchor_count(&node.anchor),
                      nx_ble_bridge_connected() ? "yes" : "no");
    }
}
