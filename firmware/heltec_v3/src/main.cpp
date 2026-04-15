/*
 * NEXUS Firmware -- Heltec WiFi LoRa 32 V3
 *
 * Full NEXUS mesh node with:
 * - SX1262 LoRa radio (via RadioLib HAL bridge)
 * - BLE bridge for phone app connectivity
 * - BLE config protocol (Meshtastic-style settings over BLE)
 * - Identity persistence in NVS
 * - Interactive OLED display with button navigation
 * - Store-and-forward persistence in flash
 * - Configurable screen timeout + deep sleep on 10s hold
 */
#include <Arduino.h>
#include <RadioLib.h>
#include <U8x8lib.h>
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

/* -- Pin definitions (Heltec V3) ---------------------------------------- */

#define LORA_SS     8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13

#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_RST    21

#define BTN_PRG     0   /* PRG button (active low, internal pullup) */
#define LED_PIN     35

/* -- Display ------------------------------------------------------------- */

/* U8x8 text-mode driver: 16 chars x 8 lines, no framebuffer needed */
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(OLED_RST, OLED_SCL, OLED_SDA);
static bool oled_ok = false;

/* -- Globals ------------------------------------------------------------- */

SX1262 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

static nx_node_t node;
static nx_identity_t stored_identity;
static nx_settings_t settings;
static nx_lora_radio_t *g_lora_radio = NULL; /* for runtime reconfiguration */
static char ble_name[20];

/* Stats */
static uint32_t rx_count = 0;
static uint32_t neighbor_count = 0;
static int last_anchor_count = 0;
static int last_session_count = 0;

/* -- UI State ------------------------------------------------------------ */

enum UIScreen {
    SCREEN_STATUS,
    SCREEN_NEIGHBORS,
    SCREEN_MAILBOX,
    SCREEN_COUNT
};

static UIScreen ui_screen = SCREEN_STATUS;
static int ui_scroll = 0;
static bool ui_dirty = true;
static uint32_t ui_last_draw_ms = 0;

#define UI_REFRESH_MS    2000

/* Button */
static bool btn_down = false;
static uint32_t btn_press_ms = 0;
static bool btn_long_fired = false;

/* Screen auto-off */
static uint32_t last_activity_ms = 0;
static bool screen_off = false;

#define BTN_DEBOUNCE_MS   50
#define BTN_LONG_MS      1000
#define BTN_SHUTDOWN_MS  10000

/* -- BLE Config Protocol ------------------------------------------------- */

/* Magic destination address for config commands (not a valid mesh addr) */
static const uint8_t CFG_MAGIC[4] = {0xFF, 0xFF, 0xFF, 0xCF};

/* Config commands (phone -> device) */
#define CFG_CMD_GET_CONFIG   0x01
#define CFG_CMD_SET_RADIO    0x02  /* [freq(4)][bw(4)][sf(1)][cr(1)][pwr(1)] */
#define CFG_CMD_SET_SCREEN   0x03  /* [timeout_ms(4)] */
#define CFG_CMD_SET_ROLE     0x04  /* [role(1)] */
#define CFG_CMD_REBOOT       0x05

/* Config response (device -> phone): CMD | 0x80 */
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

static void format_uptime(uint32_t ms, char *buf, size_t len)
{
    uint32_t s = ms / 1000;
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;
    uint32_t d = h / 24; h %= 24;
    if (d > 0)      snprintf(buf, len, "%lud %luh", (unsigned long)d, (unsigned long)h);
    else if (h > 0) snprintf(buf, len, "%luh %lum", (unsigned long)h, (unsigned long)m);
    else            snprintf(buf, len, "%lum %lus", (unsigned long)m, (unsigned long)s);
}

/* Draw a full-width inverted header bar (centered text) */
static void draw_header(const char *text)
{
    char hdr[17];
    memset(hdr, ' ', 16);
    hdr[16] = '\0';
    int len = strlen(text);
    if (len > 16) len = 16;
    int pad = (16 - len) / 2;
    memcpy(hdr + pad, text, len);
    u8x8.setInverseFont(1);
    u8x8.drawString(0, 0, hdr);
    u8x8.setInverseFont(0);
}

/* Draw a padded line (clears trailing chars from previous draws) */
static void draw_line(int row, const char *fmt, ...)
{
    char buf[17];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* Pad to 16 chars to clear old text */
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    while (n < 16) buf[n++] = ' ';
    buf[16] = '\0';
    u8x8.drawString(0, row, buf);
}

/* -- Screen: Status ------------------------------------------------------ */

static void draw_status()
{
    const nx_identity_t *id = nx_node_identity(&node);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "NEXUS  %s", role_name(node.config.role));
    draw_header(hdr);

    draw_line(1, "");

    draw_line(2, "Addr: %02X%02X%02X%02X",
              id->short_addr.bytes[0], id->short_addr.bytes[1],
              id->short_addr.bytes[2], id->short_addr.bytes[3]);

    char uptime[12];
    format_uptime(millis(), uptime, sizeof(uptime));
    draw_line(3, "Up:   %s", uptime);

    int nbrs = nx_neighbor_count(&node.route_table);
    draw_line(4, "Nbrs: %d", nbrs);

    draw_line(5, "Msgs: %lu", (unsigned long)rx_count);

    draw_line(6, "Store:%d/%d",
              nx_anchor_count(&node.anchor), node.anchor.max_slots);

    draw_line(7, "BLE:%c  LoRa:Y",
              nx_ble_bridge_connected() ? 'Y' : 'N');
}

/* -- Screen: Neighbors --------------------------------------------------- */

static void draw_neighbors()
{
    int count = nx_neighbor_count(&node.route_table);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "NEIGHBORS  %d", count);
    draw_header(hdr);

    if (count == 0) {
        draw_line(1, "");
        draw_line(2, "");
        draw_line(3, " No neighbors");
        draw_line(4, " discovered yet");
        draw_line(5, "");
        draw_line(6, "");
        draw_line(7, "");
        return;
    }

    /* Show neighbors on lines 1-6 (6 per page), line 7 = scroll hint */
    int shown = 0;
    int skip = ui_scroll;
    for (int i = 0; i < NX_MAX_NEIGHBORS && shown < 6; i++) {
        const nx_neighbor_t *n = &node.route_table.neighbors[i];
        if (!n->valid) continue;
        if (skip > 0) { skip--; continue; }

        draw_line(1 + shown, "%02X%02X%02X%02X %s",
                  n->addr.bytes[0], n->addr.bytes[1],
                  n->addr.bytes[2], n->addr.bytes[3],
                  role_name(n->role));
        shown++;
    }
    /* Clear remaining lines */
    for (int r = 1 + shown; r < 7; r++)
        draw_line(r, "");

    /* Footer */
    int pages = (count + 5) / 6;
    int page = (ui_scroll / 6) + 1;
    if (pages > 1) {
        draw_line(7, "[PRG] %d/%d", page, pages);
    } else {
        draw_line(7, "[PRG] next");
    }
}

/* -- Screen: Mailbox ----------------------------------------------------- */

static void draw_mailbox()
{
    draw_header("STORE & FORWARD");

    int count = nx_anchor_count(&node.anchor);
    draw_line(1, "");
    draw_line(2, "Slots: %d/%d", count, node.anchor.max_slots);

    uint32_t ttl_min = node.anchor.msg_ttl_ms / 60000;
    if (ttl_min >= 60)
        draw_line(3, "TTL:   %luh", (unsigned long)(ttl_min / 60));
    else
        draw_line(3, "TTL:   %lumin", (unsigned long)ttl_min);

    if (count == 0) {
        draw_line(4, "");
        draw_line(5, " No stored msgs");
        draw_line(6, "");
        draw_line(7, "[PRG] next");
        return;
    }

    /* Group messages by destination */
    struct { nx_addr_short_t addr; int count; } dests[8];
    int ndests = 0;
    int limit = node.anchor.max_slots < NX_ANCHOR_MAX_STORED ?
                node.anchor.max_slots : NX_ANCHOR_MAX_STORED;

    for (int i = 0; i < limit; i++) {
        if (!node.anchor.msgs[i].valid) continue;

        bool found = false;
        for (int d = 0; d < ndests; d++) {
            if (memcmp(dests[d].addr.bytes,
                       node.anchor.msgs[i].dest.bytes, 4) == 0) {
                dests[d].count++;
                found = true;
                break;
            }
        }
        if (!found && ndests < 8) {
            dests[ndests].addr = node.anchor.msgs[i].dest;
            dests[ndests].count = 1;
            ndests++;
        }
    }

    draw_line(4, "");
    for (int d = 0; d < ndests && d < 3; d++) {
        draw_line(5 + d, "%02X%02X%02X%02X  x%d",
                  dests[d].addr.bytes[0], dests[d].addr.bytes[1],
                  dests[d].addr.bytes[2], dests[d].addr.bytes[3],
                  dests[d].count);
    }
    /* Clear remaining */
    for (int r = 5 + (ndests < 3 ? ndests : 3); r < 7; r++)
        draw_line(r, "");

    draw_line(7, "[PRG] next");
}

/* -- Screen dispatch ----------------------------------------------------- */

static void draw_screen()
{
    if (!oled_ok || screen_off) return;

    switch (ui_screen) {
    case SCREEN_STATUS:    draw_status(); break;
    case SCREEN_NEIGHBORS: draw_neighbors(); break;
    case SCREEN_MAILBOX:   draw_mailbox(); break;
    default: break;
    }
}

/* -- Screen timeout ------------------------------------------------------ */

static void screen_wake()
{
    if (!oled_ok || !screen_off) return;
    screen_off = false;
    u8x8.setPowerSave(0);
    ui_dirty = true;
    Serial.println("[OLED] Screen on");
}

static void screen_sleep()
{
    if (!oled_ok || screen_off) return;
    screen_off = true;
    u8x8.setPowerSave(1);
    Serial.println("[OLED] Screen off (timeout)");
}

/* -- Button handler ------------------------------------------------------ */

static void button_update()
{
    bool pressed = (digitalRead(BTN_PRG) == LOW);
    uint32_t now = millis();

    if (pressed && !btn_down) {
        /* Just pressed */
        btn_down = true;
        btn_press_ms = now;
        btn_long_fired = false;
        last_activity_ms = now;

        /* Wake screen on any press (consume this press for wake only) */
        if (screen_off) {
            screen_wake();
            btn_long_fired = true; /* suppress further actions for this press */
        }
    } else if (pressed && btn_down) {
        /* Held -- check for long press thresholds */
        uint32_t held = now - btn_press_ms;

        if (held > BTN_SHUTDOWN_MS) {
            /* 10s hold: deep sleep / shutdown */
            Serial.println("[BTN] Shutting down...");
            nx_anchor_store_save(&node.anchor);
            nx_settings_save(&settings);
            nx_node_stop(&node);

            if (oled_ok) {
                if (screen_off) {
                    screen_off = false;
                    u8x8.setPowerSave(0);
                }
                u8x8.clear();
                draw_header("SHUTTING DOWN");
                draw_line(3, "  Hold released");
                draw_line(4, "  to wake up");
                delay(1500);
                u8x8.clear();
                u8x8.setPowerSave(1);
            }

            digitalWrite(LED_PIN, LOW);
            /* ESP32 deep sleep -- wakes on PRG button (GPIO0 LOW) */
            esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PRG, 0);
            esp_deep_sleep_start();

        } else if (held > BTN_LONG_MS && !btn_long_fired) {
            btn_long_fired = true;
            /* 1s hold: send announcement */
            nx_node_announce(&node);
            Serial.println("[BTN] Announce sent");

            /* Brief LED flash for feedback */
            digitalWrite(LED_PIN, LOW);
            delay(100);
            digitalWrite(LED_PIN, HIGH);

            ui_dirty = true;
        }
    } else if (!pressed && btn_down) {
        /* Released */
        btn_down = false;
        if (!btn_long_fired && (now - btn_press_ms > BTN_DEBOUNCE_MS)) {
            /* Short press: cycle screens */
            last_activity_ms = now;
            if (ui_screen == SCREEN_NEIGHBORS) {
                int count = nx_neighbor_count(&node.route_table);
                if (ui_scroll + 6 < count) {
                    /* Scroll to next page of neighbors */
                    ui_scroll += 6;
                } else {
                    /* Past last page -- next screen */
                    ui_scroll = 0;
                    ui_screen = (UIScreen)((int)ui_screen + 1);
                    if ((int)ui_screen >= SCREEN_COUNT)
                        ui_screen = SCREEN_STATUS;
                }
            } else {
                ui_scroll = 0;
                ui_screen = (UIScreen)((int)ui_screen + 1);
                if ((int)ui_screen >= SCREEN_COUNT)
                    ui_screen = SCREEN_STATUS;
            }
            ui_dirty = true;
        }
    }
}

/* -- Callbacks ----------------------------------------------------------- */

static void on_data(const nx_addr_short_t *src,
                    const uint8_t *data, size_t len, void *user)
{
    (void)user;
    rx_count++;
    ui_dirty = true;
    Serial.printf("[RX] From %02X%02X%02X%02X len=%d\n",
                  src->bytes[0], src->bytes[1],
                  src->bytes[2], src->bytes[3], (int)len);

    char ebuf[NX_EVENT_TEXT_MAX];
    snprintf(ebuf, sizeof(ebuf), "rx %02X%02X%02X%02X %dB",
             src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3], (int)len);
    nx_event_log(ebuf);

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
    ui_dirty = true;
    Serial.printf("[NBR] %02X%02X%02X%02X role=%d\n",
                  addr->bytes[0], addr->bytes[1],
                  addr->bytes[2], addr->bytes[3], role);

    char ebuf[NX_EVENT_TEXT_MAX];
    snprintf(ebuf, sizeof(ebuf), "nbr %02X%02X%02X%02X r=%d",
             addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3], role);
    nx_event_log(ebuf);
}

static void on_session(const nx_addr_short_t *src,
                       const uint8_t *data, size_t len, void *user)
{
    (void)user;
    rx_count++;
    ui_dirty = true;
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
    /* Response: [MAGIC(4)][0x81][freq(4)][bw(4)][sf][cr][pwr][timeout(4)][role][addr(4)] = 25B */
    uint8_t resp[25];
    memcpy(resp, CFG_MAGIC, 4);
    resp[4] = CFG_CMD_GET_CONFIG | CFG_RESP_FLAG;

    const nx_lora_config_t *lc = &settings.lora_config;
    /* Little-endian encoding */
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
    resp[16] = (uint8_t)(settings.screen_timeout_ms);
    resp[17] = (uint8_t)(settings.screen_timeout_ms >> 8);
    resp[18] = (uint8_t)(settings.screen_timeout_ms >> 16);
    resp[19] = (uint8_t)(settings.screen_timeout_ms >> 24);
    resp[20] = settings.node_role;

    const nx_identity_t *id = nx_node_identity(&node);
    memcpy(&resp[21], id->short_addr.bytes, 4);

    /* Send config response directly -- bridge adds NUS [LEN_HI][LEN_LO] framing */
    nx_ble_bridge_send(resp, sizeof(resp));
}

static void handle_ble_config(const uint8_t *payload, size_t len)
{
    /* payload starts after the 4-byte magic: [CMD][params...] */
    if (len < 1) return;

    uint8_t cmd = payload[0];

    switch (cmd) {
    case CFG_CMD_GET_CONFIG:
        Serial.println("[CFG] GET_CONFIG");
        send_config_response();
        break;

    case CFG_CMD_SET_RADIO:
        if (len < 12) break; /* need cmd(1) + freq(4) + bw(4) + sf(1) + cr(1) + pwr(1) */
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

            /* Validate */
            if (freq < 137000000 || freq > 1020000000) break;
            if (sf < 7 || sf > 12) break;
            if (cr < 5 || cr > 8) break;
            if (pwr < 2 || pwr > 22) break;

            settings.lora_config.frequency_hz = freq;
            settings.lora_config.bandwidth_hz = bw;
            settings.lora_config.spreading_factor = sf;
            settings.lora_config.coding_rate = cr;
            settings.lora_config.tx_power_dbm = pwr;

            /* Apply to radio */
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
        if (len < 5) break; /* cmd(1) + timeout(4) */
        {
            uint32_t timeout = (uint32_t)payload[1] |
                               ((uint32_t)payload[2] << 8) |
                               ((uint32_t)payload[3] << 16) |
                               ((uint32_t)payload[4] << 24);
            settings.screen_timeout_ms = timeout;
            nx_settings_save(&settings);
            Serial.printf("[CFG] SET_SCREEN timeout=%lu ms\n",
                          (unsigned long)timeout);
            send_config_response();
        }
        break;

    case CFG_CMD_SET_ROLE:
        if (len < 2) break; /* cmd(1) + role(1) */
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
        Serial.printf("[SETTINGS] screen_timeout=%lu role=%d(%s)\n",
                      (unsigned long)settings.screen_timeout_ms,
                      settings.node_role, role_name(settings.node_role));
        Serial.printf("[SETTINGS] freq=%lu bw=%lu sf=%d cr=%d pwr=%d\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      (unsigned long)settings.lora_config.bandwidth_hz,
                      settings.lora_config.spreading_factor,
                      settings.lora_config.coding_rate,
                      settings.lora_config.tx_power_dbm);
    } else if (strcmp(line, "BATTERY") == 0) {
        int32_t mv = battery_read_mv();
        int pct = battery_percent();
        if (mv < 0) Serial.println("[BATTERY] unsupported");
        else Serial.printf("[BATTERY] %ld mV (%d%%)\n", (long)mv, pct);
    } else if (strcmp(line, "EVENTS") == 0) {
        nx_event_t ev[NX_EVENT_RING_SIZE];
        size_t n = nx_event_recent(ev, NX_EVENT_RING_SIZE);
        Serial.printf("[EVENTS] %u recent\n", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            Serial.printf("  +%lus  %s\n", (unsigned long)ev[i].timestamp_s, ev[i].text);
        }
    } else if (strcmp(line, "HELP") == 0) {
        Serial.println("Commands: STATUS ANNOUNCE NEIGHBORS MAILBOX RADIO SETTINGS BATTERY EVENTS HELP");
    }
}

/* -- Setup --------------------------------------------------------------- */

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[NEXUS] Heltec V3 starting...");

    /* GPIO */
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BTN_PRG, INPUT_PULLUP);

    /* Load settings (or use defaults on first boot) */
    if (nx_settings_load(&settings) != NX_OK) {
        Serial.println("[NEXUS] First boot - using default settings");
        nx_settings_t defaults = NX_SETTINGS_DEFAULT;
        memcpy(&settings, &defaults, sizeof(settings));
        nx_settings_save(&settings);
    } else {
        Serial.printf("[NEXUS] Settings loaded: freq=%lu sf=%d role=%d timeout=%lu\n",
                      (unsigned long)settings.lora_config.frequency_hz,
                      settings.lora_config.spreading_factor,
                      settings.node_role,
                      (unsigned long)settings.screen_timeout_ms);
    }

    /* OLED display */
    oled_ok = u8x8.begin();
    if (oled_ok) {
        u8x8.setFont(u8x8_font_chroma48medium8_r);
        u8x8.clear();

        /* Splash screen */
        draw_header("NEXUS MESH");
        draw_line(2, "   LoRa + BLE");
        draw_line(3, " E2E Encrypted");
        draw_line(5, "    v0.1.0");
        draw_line(7, " Starting...");

        Serial.println("[OLED] OK");
    } else {
        Serial.println("[OLED] Not found");
    }

    /* Transport registry */
    nx_transport_registry_init();

    /* LoRa radio via RadioLib HAL -- keep radio pointer for reconfiguration */
    g_lora_radio = nx_radiolib_create(&radio);
    if (!g_lora_radio || g_lora_radio->ops->init(g_lora_radio, &settings.lora_config) != NX_OK) {
        Serial.println("[NEXUS] LoRa radio init failed!");
        if (oled_ok) {
            u8x8.clear();
            draw_header("!! ERROR !!");
            draw_line(3, " LoRa FAILED");
            draw_line(5, " Check wiring");
        }
        while (1) delay(1000);
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
        if (oled_ok) {
            u8x8.clear();
            draw_header("!! ERROR !!");
            draw_line(3, " Node init FAIL");
        }
        while (1) delay(1000);
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

    Serial.println("[NEXUS] Ready - press PRG to cycle screens, hold for announce");
    Serial.println("[NEXUS] Serial commands: STATUS ANNOUNCE NEIGHBORS MAILBOX RADIO SETTINGS HELP");
    digitalWrite(LED_PIN, HIGH);

    /* Clear splash and draw initial screen */
    delay(1500);
    if (oled_ok) {
        u8x8.clear();
        draw_screen();
    }
    ui_dirty = false;
    ui_last_draw_ms = millis();
    last_activity_ms = millis();
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
            /* Check for config magic prefix */
            if (ble_len >= 5 && memcmp(ble_buf, CFG_MAGIC, 4) == 0) {
                handle_ble_config(&ble_buf[4], ble_len - 4);
            } else if (ble_len > 4) {
                /* Normal mesh packet: [dest(4)][data...] */
                nx_addr_short_t dest;
                memcpy(dest.bytes, ble_buf, 4);
                nx_node_send(&node, &dest, &ble_buf[4], ble_len - 4);
            }
        }
    }

    nx_ble_bridge_poll();

    /* Button navigation */
    button_update();

    /* Persist anchor mailbox when contents change */
    int cur_anchor = nx_anchor_count(&node.anchor);
    if (cur_anchor != last_anchor_count) {
        nx_anchor_store_save(&node.anchor);
        Serial.printf("[ANCHOR] Saved %d messages to flash\n", cur_anchor);
        last_anchor_count = cur_anchor;
        ui_dirty = true;
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

    /* Screen timeout */
    uint32_t now = millis();
    if (oled_ok && !screen_off && settings.screen_timeout_ms > 0) {
        if (now - last_activity_ms > settings.screen_timeout_ms) {
            screen_sleep();
        }
    }

    /* Update display */
    if (!screen_off && (ui_dirty || (now - ui_last_draw_ms > UI_REFRESH_MS))) {
        draw_screen();
        ui_dirty = false;
        ui_last_draw_ms = now;
    }

    /* Periodic serial status */
    if (now - last_status_ms > 30000) {
        last_status_ms = now;

        const nx_identity_t *id = nx_node_identity(&node);
        int32_t bat_mv = battery_read_mv();
        int bat_pct = battery_percent();
        Serial.printf("[STATUS] %02X%02X%02X%02X nbrs=%d msgs=%lu stored=%d BLE=%s bat=%ldmV(%d%%)\n",
                      id->short_addr.bytes[0], id->short_addr.bytes[1],
                      id->short_addr.bytes[2], id->short_addr.bytes[3],
                      nx_neighbor_count(&node.route_table),
                      (unsigned long)rx_count,
                      nx_anchor_count(&node.anchor),
                      nx_ble_bridge_connected() ? "yes" : "no",
                      (long)bat_mv, bat_pct);
    }
}
