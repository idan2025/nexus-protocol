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
#include <RadioLib.h>

/* NEXUS C API */
extern "C" {
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/lora_radio.h"
}

#include "radiolib_hal.h"
#include "anchor_store.h"

/* ── Pin definitions (RAK4631 WisBlock) ───────────────────────────────── */

#define LORA_SS     SS
#define LORA_DIO1   WB_IO1
#define LORA_RST    WB_IO2
#define LORA_BUSY   WB_IO3

/* LED_GREEN and LED_BLUE are defined in variant.h */

/* ── Globals ──────────────────────────────────────────────────────────── */

SX1262 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

static nx_node_t node;
static uint32_t msg_count = 0;
static uint32_t neighbor_count = 0;
static int last_anchor_count = 0;

/* ── Flash identity storage (nRF52 InternalFS) ────────────────────────── */

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;
static File id_file(InternalFS);
static const char *ID_FILE = "/nexus_id";

static nx_err_t load_identity(nx_identity_t *id)
{
    InternalFS.begin();
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
    Serial.begin(115200);
    delay(2000); /* nRF52 USB serial needs extra time */
    Serial.println("\n[NEXUS] RAK4631 starting...");

    /* LEDs */
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLUE, LOW);

    /* Transport registry */
    nx_transport_registry_init();

    /* LoRa radio via RadioLib HAL */
    nx_lora_config_t lora_cfg = NX_LORA_CONFIG_DEFAULT;
    nx_transport_t *lora_t = nx_radiolib_transport_setup(&radio, &lora_cfg);
    if (!lora_t) {
        Serial.println("[NEXUS] LoRa transport setup failed!");
        while (1) {
            digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
            delay(200);
        }
    }
    Serial.println("[NEXUS] LoRa transport OK");

    /* Load or generate identity */
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

    /* Node config */
    nx_node_config_t cfg = {
        .role              = NX_ROLE_RELAY,
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
        while (1) delay(1000);
    }

    const nx_identity_t *id = nx_node_identity(&node);
    Serial.printf("[NEXUS] Node: %02X%02X%02X%02X %s\n",
                  id->short_addr.bytes[0], id->short_addr.bytes[1],
                  id->short_addr.bytes[2], id->short_addr.bytes[3],
                  new_identity ? "(new)" : "(stored)");

    /* Load stored messages from flash */
    if (nx_anchor_store_load(&node.anchor) == NX_OK) {
        int count = nx_anchor_count(&node.anchor);
        Serial.printf("[NEXUS] Loaded %d stored messages from flash\n", count);
        last_anchor_count = count;
    }

    /* Initial announcement */
    nx_node_announce(&node);

    digitalWrite(LED_GREEN, HIGH);
    Serial.println("[NEXUS] Ready");
}

/* ── Main Loop ────────────────────────────────────────────────────────── */

static uint32_t last_status_ms = 0;

void loop()
{
    nx_node_poll(&node, 10);

    /* Persist anchor mailbox when contents change */
    int cur_anchor = nx_anchor_count(&node.anchor);
    if (cur_anchor != last_anchor_count) {
        nx_anchor_store_save(&node.anchor);
        Serial.printf("[ANCHOR] Saved %d messages to flash\n", cur_anchor);
        last_anchor_count = cur_anchor;
    }

    uint32_t now = millis();
    if (now - last_status_ms > 30000) {
        last_status_ms = now;

        const nx_identity_t *id = nx_node_identity(&node);
        Serial.printf("[STATUS] %02X%02X%02X%02X neighbors=%lu msgs=%lu stored=%d\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      (unsigned long)neighbor_count,
                      (unsigned long)msg_count,
                      nx_anchor_count(&node.anchor));

        /* Blink LED */
        digitalWrite(LED_BLUE, HIGH);
        delay(50);
        digitalWrite(LED_BLUE, LOW);
    }
}
