/*
 * NEXUS Firmware -- Heltec WiFi LoRa 32 V3
 *
 * Full NEXUS mesh node with:
 * - SX1262 LoRa radio (via RadioLib HAL bridge)
 * - BLE bridge for phone app connectivity
 * - Identity persistence in NVS
 * - OLED status display
 *
 * The node operates independently as a mesh relay.
 * When a phone connects via BLE, it also bridges packets
 * between the phone app and the LoRa mesh.
 */
#include <Arduino.h>
#include <RadioLib.h>
#include <Wire.h>

/* NEXUS C API */
extern "C" {
#include "nexus/node.h"
#include "nexus/transport.h"
#include "nexus/lora_radio.h"
}

#include "radiolib_hal.h"
#include "ble_bridge.h"
#include "identity_store.h"

/* ── Pin definitions (Heltec V3) ──────────────────────────────────────── */

#define LORA_SS     8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13

#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_RST    21

#define BTN_PRG     0   /* PRG button (active low) */
#define LED_PIN     35

/* ── Globals ──────────────────────────────────────────────────────────── */

SX1262 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

static nx_node_t node;
static nx_identity_t stored_identity;
static char ble_name[20];

/* Stats */
static uint32_t msg_count = 0;
static uint32_t neighbor_count = 0;
static int16_t last_rssi = 0;

/* ── OLED Display (SSD1306 via I2C, minimal driver) ───────────────────── */

/* Using direct I2C commands to avoid library dependency */
#define OLED_ADDR  0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

static bool oled_ok = false;

static void oled_cmd(uint8_t cmd)
{
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x00);  /* command mode */
    Wire.write(cmd);
    Wire.endTransmission();
}

static void oled_init(void)
{
    /* Reset OLED */
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    delay(20);

    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    /* Check if OLED responds */
    Wire.beginTransmission(OLED_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[OLED] Not found");
        return;
    }

    /* SSD1306 init sequence */
    oled_cmd(0xAE); /* display off */
    oled_cmd(0xD5); oled_cmd(0x80); /* clock div */
    oled_cmd(0xA8); oled_cmd(0x3F); /* multiplex 64 */
    oled_cmd(0xD3); oled_cmd(0x00); /* offset 0 */
    oled_cmd(0x40); /* start line 0 */
    oled_cmd(0x8D); oled_cmd(0x14); /* charge pump on */
    oled_cmd(0x20); oled_cmd(0x00); /* horizontal addressing */
    oled_cmd(0xA1); /* segment remap */
    oled_cmd(0xC8); /* COM scan dec */
    oled_cmd(0xDA); oled_cmd(0x12); /* COM pins */
    oled_cmd(0x81); oled_cmd(0xCF); /* contrast */
    oled_cmd(0xD9); oled_cmd(0xF1); /* precharge */
    oled_cmd(0xDB); oled_cmd(0x40); /* VCOMH */
    oled_cmd(0xA4); /* display from RAM */
    oled_cmd(0xA6); /* normal (not inverted) */
    oled_cmd(0xAF); /* display on */

    oled_ok = true;
    Serial.println("[OLED] OK");
}

static void oled_clear(void)
{
    if (!oled_ok) return;
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127); /* col range */
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);   /* page range */

    for (int i = 0; i < 1024; i++) {
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x40); /* data mode */
        for (int j = 0; j < 16 && i < 1024; j++, i++) {
            Wire.write(0x00);
        }
        i--; /* compensate for loop increment */
        Wire.endTransmission();
    }
}

/* Simple 5x7 font rendering -- just prints ASCII to serial for now.
 * Full OLED text rendering would use a font table. For production,
 * use U8g2 or a custom minimal font. */
static void oled_print_status(const char *line1, const char *line2,
                               const char *line3, const char *line4)
{
    if (!oled_ok) return;
    /* For now, just log to serial. A real implementation would
     * render text to the framebuffer using a bitmap font. */
    (void)line1; (void)line2; (void)line3; (void)line4;
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

    /* Forward to phone if connected */
    if (nx_ble_bridge_connected()) {
        /* Reconstruct a minimal packet frame for the phone:
         * [src(4)][len(2)][data] */
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
    msg_count++;
    Serial.printf("[SESSION] From %02X%02X%02X%02X len=%d\n",
                  src->bytes[0], src->bytes[1],
                  src->bytes[2], src->bytes[3], (int)len);

    /* Forward session messages to phone */
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

/* ── Setup ────────────────────────────────────────────────────────────── */

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[NEXUS] Heltec V3 starting...");

    /* LED */
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    /* OLED */
    oled_init();
    oled_clear();

    /* Transport registry */
    nx_transport_registry_init();

    /* LoRa radio via RadioLib HAL */
    nx_lora_config_t lora_cfg = NX_LORA_CONFIG_DEFAULT;
    nx_transport_t *lora_t = nx_radiolib_transport_setup(&radio, &lora_cfg);
    if (!lora_t) {
        Serial.println("[NEXUS] LoRa transport setup failed!");
        while (1) delay(1000);
    }
    Serial.println("[NEXUS] LoRa transport OK");

    /* Load or generate identity */
    bool new_identity = false;
    if (nx_identity_store_load(&stored_identity) != NX_OK) {
        Serial.println("[NEXUS] No stored identity, generating new...");
        nx_identity_generate(&stored_identity);
        nx_identity_store_save(&stored_identity);
        new_identity = true;
    } else {
        Serial.println("[NEXUS] Identity loaded from NVS");
    }

    /* Node config */
    nx_node_config_t cfg = {
        .role              = NX_ROLE_RELAY,
        .default_ttl       = 7,
        .beacon_interval_ms = 30000,
        .on_data           = on_data,
        .on_neighbor       = on_neighbor,
        .on_session        = on_session,
        .on_group          = NULL,
        .user_ctx          = NULL,
    };

    if (nx_node_init_with_identity(&node, &cfg, &stored_identity) != NX_OK) {
        Serial.println("[NEXUS] Node init failed!");
        while (1) delay(1000);
    }

    const nx_identity_t *id = nx_node_identity(&node);
    snprintf(ble_name, sizeof(ble_name), "NEXUS-%02X%02X",
             id->short_addr.bytes[0], id->short_addr.bytes[1]);

    Serial.printf("[NEXUS] Node: %02X%02X%02X%02X %s\n",
                  id->short_addr.bytes[0], id->short_addr.bytes[1],
                  id->short_addr.bytes[2], id->short_addr.bytes[3],
                  new_identity ? "(new)" : "(stored)");

    /* BLE bridge for phone connectivity */
    nx_ble_bridge_init(ble_name);
    nx_ble_bridge_start();

    /* Initial announcement */
    nx_node_announce(&node);

    Serial.println("[NEXUS] Ready");
    digitalWrite(LED_PIN, HIGH);
}

/* ── Main Loop ────────────────────────────────────────────────────────── */

static uint32_t last_status_ms = 0;

void loop()
{
    /* Run NEXUS event loop */
    nx_node_poll(&node, 10);

    /* Check for packets from phone via BLE */
    if (nx_ble_bridge_connected()) {
        uint8_t ble_buf[NX_MAX_PACKET];
        size_t ble_len = 0;

        while (nx_ble_bridge_recv(ble_buf, sizeof(ble_buf), &ble_len) == NX_OK) {
            /* Phone sent a packet -- inject into mesh.
             * Format: [dest(4)][data...] */
            if (ble_len > 4) {
                nx_addr_short_t dest;
                memcpy(dest.bytes, ble_buf, 4);
                nx_node_send(&node, &dest, &ble_buf[4], ble_len - 4);
            }
        }
    }

    /* BLE polling */
    nx_ble_bridge_poll();

    /* Periodic status output */
    uint32_t now = millis();
    if (now - last_status_ms > 30000) {
        last_status_ms = now;

        const nx_identity_t *id = nx_node_identity(&node);
        Serial.printf("[STATUS] %02X%02X%02X%02X neighbors=%lu msgs=%lu BLE=%s\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      (unsigned long)neighbor_count,
                      (unsigned long)msg_count,
                      nx_ble_bridge_connected() ? "yes" : "no");

        char l1[32], l2[32], l3[32], l4[32];
        snprintf(l1, sizeof(l1), "NEXUS %02X%02X%02X%02X",
                 id->short_addr.bytes[0], id->short_addr.bytes[1],
                 id->short_addr.bytes[2], id->short_addr.bytes[3]);
        snprintf(l2, sizeof(l2), "Neighbors: %lu", (unsigned long)neighbor_count);
        snprintf(l3, sizeof(l3), "Messages: %lu", (unsigned long)msg_count);
        snprintf(l4, sizeof(l4), "BLE: %s", nx_ble_bridge_connected() ? "Connected" : "---");
        oled_print_status(l1, l2, l3, l4);
    }
}
