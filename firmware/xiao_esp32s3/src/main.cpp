/*
 * NEXUS Firmware -- Seeed XIAO ESP32S3 + WIO-SX1262
 *
 * Headless NEXUS mesh node with:
 * - SX1262 LoRa radio (via RadioLib HAL bridge)
 * - BLE bridge for phone app connectivity
 * - BLE config protocol (settings over BLE)
 * - Identity persistence in NVS
 * - Store-and-forward persistence in flash
 * - WiFi available (ESP32-S3 integrated, reserved for gateway mode)
 * - No display -- status via serial + LED + BLE
 * - BOOT button: short press = announce, long hold = deep sleep
 */
#include <Arduino.h>
#include <RadioLib.h>
#include <esp_sleep.h>

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
#include "identity_store.h"
#include "anchor_store.h"
#include "settings_store.h"
#include "battery.h"
#include "event_ring.h"
#include "session_store.h"

/* -- Pin definitions (XIAO ESP32S3 + WIO-SX1262 expansion) --------------- */
/*
 * WIO-SX1262 expansion board connects to XIAO header pins:
 *   D0 (GPIO1)  -> SX1262 RESET
 *   D1 (GPIO2)  -> SX1262 BUSY
 *   D2 (GPIO3)  -> SX1262 DIO1
 *   D3 (GPIO4)  -> SX1262 NSS (chip select)
 *   D8 (GPIO7)  -> SPI SCK   (default SPI bus)
 *   D9 (GPIO8)  -> SPI MISO  (default SPI bus)
 *   D10(GPIO9)  -> SPI MOSI  (default SPI bus)
 */
#define LORA_SS     4   /* D3 = GPIO4 */
#define LORA_DIO1   3   /* D2 = GPIO3 */
#define LORA_RST    1   /* D0 = GPIO1 */
#define LORA_BUSY   2   /* D1 = GPIO2 */

/* XIAO ESP32S3 built-in LED (active LOW on most variants) */
#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif
#define LED_PIN     LED_BUILTIN

/* BOOT button (active LOW) -- GPIO0 on ESP32S3 XIAO */
#define BTN_BOOT    0

/* -- Globals ------------------------------------------------------------- */

SX1262 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

static nx_node_t node;
static nx_identity_t stored_identity;
static nx_settings_t settings;
static nx_lora_radio_t *g_lora_radio = NULL;
static char ble_name[20];

/* Stats */
static uint32_t rx_count = 0;
static uint32_t neighbor_count = 0;
static int last_anchor_count = 0;
static int last_session_count = 0;

/* Button */
static bool btn_down = false;
static uint32_t btn_press_ms = 0;
static bool btn_long_fired = false;

#define BTN_DEBOUNCE_MS   50
#define BTN_LONG_MS      1000
#define BTN_SHUTDOWN_MS  10000

/* LED blink state */
static uint32_t led_off_ms = 0;

/* -- BLE Config Protocol ------------------------------------------------- */

static const uint8_t CFG_MAGIC[4] = {0xFF, 0xFF, 0xFF, 0xCF};

#define CFG_CMD_GET_CONFIG   0x01
#define CFG_CMD_SET_RADIO    0x02
#define CFG_CMD_SET_SCREEN   0x03  /* ignored on headless node */
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
    digitalWrite(LED_PIN, LOW);  /* active LOW */
    led_off_ms = millis() + duration_ms;
}

static void led_double_blink()
{
    digitalWrite(LED_PIN, LOW);
    delay(60);
    digitalWrite(LED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_PIN, LOW);
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

    led_blink(50);

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
    /* screen_timeout field (headless node sends 0 = not applicable) */
    resp[16] = 0;
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
        /* Headless node -- acknowledge but ignore screen timeout */
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
        ESP.restart();
        break;

    default:
        Serial.printf("[CFG] Unknown cmd 0x%02X\n", cmd);
        break;
    }
}

/* -- Button handler ------------------------------------------------------ */

static void button_update()
{
    bool pressed = (digitalRead(BTN_BOOT) == LOW);
    uint32_t now = millis();

    if (pressed && !btn_down) {
        btn_down = true;
        btn_press_ms = now;
        btn_long_fired = false;
    } else if (pressed && btn_down) {
        uint32_t held = now - btn_press_ms;

        if (held > BTN_SHUTDOWN_MS) {
            /* 10s hold: deep sleep */
            Serial.println("[BTN] Shutting down...");
            nx_anchor_store_save(&node.anchor);
            nx_settings_save(&settings);
            nx_node_stop(&node);

            /* Blink LED rapidly to confirm shutdown */
            for (int i = 0; i < 6; i++) {
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
                delay(150);
            }
            digitalWrite(LED_PIN, HIGH); /* LED off */

            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);
            esp_deep_sleep_start();

        } else if (held > BTN_LONG_MS && !btn_long_fired) {
            btn_long_fired = true;
            nx_node_announce(&node);
            Serial.println("[BTN] Announce sent");
            led_blink(200);
        }
    } else if (!pressed && btn_down) {
        btn_down = false;
        if (!btn_long_fired && (now - btn_press_ms > BTN_DEBOUNCE_MS)) {
            /* Short press: announce */
            nx_node_announce(&node);
            Serial.println("[BTN] Announce sent");
            led_blink(100);
        }
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
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[NEXUS] XIAO ESP32S3 + WIO-SX1262 starting...");

    /* GPIO */
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  /* LED off (active LOW) */
    pinMode(BTN_BOOT, INPUT_PULLUP);

    /* Load settings (or use defaults on first boot) */
    if (nx_settings_load(&settings) != NX_OK) {
        Serial.println("[NEXUS] First boot - using default settings");
        nx_settings_t defaults = NX_SETTINGS_DEFAULT;
        memcpy(&settings, &defaults, sizeof(settings));
        /* Headless node: disable screen timeout */
        settings.screen_timeout_ms = 0;
        nx_settings_save(&settings);
    } else {
        Serial.printf("[NEXUS] Settings loaded: freq=%lu sf=%d role=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      settings.lora_config.spreading_factor,
                      settings.node_role);
    }

    /* Transport registry */
    nx_transport_registry_init();

    /* LoRa radio via RadioLib HAL */
    g_lora_radio = nx_radiolib_create(&radio);
    if (!g_lora_radio || g_lora_radio->ops->init(g_lora_radio, &settings.lora_config) != NX_OK) {
        Serial.println("[NEXUS] LoRa radio init failed!");
        Serial.println("[NEXUS] Check WIO-SX1262 expansion board connection");
        /* Rapid blink to signal error */
        while (1) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(200);
        }
    }

    /* Create and register LoRa transport */
    nx_transport_t *lora_t = nx_lora_transport_create();
    if (!lora_t || lora_t->ops->init(lora_t, &g_lora_radio) != NX_OK) {
        Serial.println("[NEXUS] LoRa transport init failed!");
        while (1) delay(1000);
    }
    if (nx_transport_register(lora_t) != NX_OK) {
        Serial.println("[NEXUS] LoRa transport register failed!");
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
        .role              = (nx_role_t)settings.node_role,
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
        while (1) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(500);
        }
    }

    const nx_identity_t *id = nx_node_identity(&node);
    snprintf(ble_name, sizeof(ble_name), "NEXUS-%02X%02X",
             id->short_addr.bytes[0], id->short_addr.bytes[1]);

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

    /* Resume Double Ratchet sessions from flash */
    if (nx_session_store_load(&node.sessions) == NX_OK) {
        last_session_count = nx_session_count(&node.sessions);
        Serial.printf("[NEXUS] Resumed %d sessions from flash\n",
                      last_session_count);
    }

    /* BLE bridge for phone connectivity */
    nx_ble_bridge_init(ble_name);
    nx_ble_bridge_start();

    battery_init();
    nx_event_log("boot");

    nx_node_announce(&node);

    Serial.println("[NEXUS] Ready (headless mode)");
    Serial.println("[NEXUS] BOOT button: press=announce, long hold=sleep");
    Serial.println("[NEXUS] Serial commands: STATUS ANNOUNCE NEIGHBORS MAILBOX RADIO SETTINGS HELP");

    /* Steady LED = running */
    digitalWrite(LED_PIN, LOW);
    delay(500);
    digitalWrite(LED_PIN, HIGH);
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

    /* Button handling */
    button_update();

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

    /* LED auto-off after blink */
    if (led_off_ms > 0 && millis() >= led_off_ms) {
        digitalWrite(LED_PIN, HIGH); /* off */
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

    /* Periodic serial status + heartbeat blink */
    uint32_t now = millis();
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

        /* Brief heartbeat blink */
        led_blink(30);
    }
}
