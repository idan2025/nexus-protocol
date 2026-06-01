/*
 * NEXUS Firmware -- RadioLib HAL Bridge
 *
 * Maps nx_lora_radio_ops_t to RadioLib's SX1262 C++ API.
 *
 * RX is interrupt-driven: startReceive() once, DIO1 ISR sets a flag, the
 * receive() HAL op polls the flag with yield() up to timeout_ms. This keeps
 * the main loop cooperative with BLE/FreeRTOS instead of blocking ~100 s
 * inside RadioLib's synchronous receive().
 */
#include "radiolib_hal.h"
#include <string.h>
#include <Arduino.h>

extern "C" {
#include "nexus/platform.h"
}

#if defined(ESP32) || defined(ESP_PLATFORM)
  #define NX_ISR_ATTR IRAM_ATTR
#else
  #define NX_ISR_ATTR
#endif

/* Verbose LoRa logging: when set, every TX/RX event is printed to
 * Serial so "no comms" symptoms can be diagnosed without a logic
 * analyzer. Leave on for the v0.6.x debug builds; gate off for v0.7.0
 * release builds via -DNX_LORA_VERBOSE=0. */
#ifndef NX_LORA_VERBOSE
#define NX_LORA_VERBOSE 1
#endif

#if NX_LORA_VERBOSE
  #define LORA_LOG(...) do { Serial.printf("[LORA] " __VA_ARGS__); } while (0)
#else
  #define LORA_LOG(...) do {} while (0)
#endif

/* ── Internal state ───────────────────────────────────────────────────── */

typedef struct {
    SX1262 *rl;        /* RadioLib SX1262 instance (not owned) */
} radiolib_hw_t;

/* Single-radio firmware: one flag is enough. If multi-radio is ever needed,
 * move this into radiolib_hw_t and bind through a small trampoline. */
static volatile bool s_rx_flag = false;
static bool s_rx_armed = false;

static void NX_ISR_ATTR rl_dio1_isr(void)
{
    s_rx_flag = true;
}

static inline SX1262 *get_rl(nx_lora_radio_t *radio)
{
    return ((radiolib_hw_t *)radio->hw)->rl;
}

static void rl_arm_rx(SX1262 *rl)
{
    s_rx_flag = false;
    /* Re-register ISR every time: RadioLib's scanChannel() overwrites the
     * DIO1 action for CAD-done, so any call to rl_cad() disconnects our
     * packet-received ISR.  Always restore it before startReceive(). */
    rl->setPacketReceivedAction(rl_dio1_isr);
    rl->startReceive();
    s_rx_armed = true;
}

/* ── HAL: init ────────────────────────────────────────────────────────── */

static nx_err_t rl_init(nx_lora_radio_t *radio, const nx_lora_config_t *config)
{
    SX1262 *rl = get_rl(radio);

    float freq   = (float)config->frequency_hz / 1e6f;
    float bw     = (float)config->bandwidth_hz / 1e3f;
    uint8_t sf   = config->spreading_factor;
    uint8_t cr   = config->coding_rate;
    uint8_t sw   = config->sync_word;
    int8_t power = config->tx_power_dbm;
    uint16_t pre = config->preamble_len;

    float tcxo = config->tcxo_voltage;
    int state = rl->begin(freq, bw, sf, cr, sw, power, pre, tcxo);
    if (state != RADIOLIB_ERR_NONE) {
        /* Surface the numeric code so "check wiring" failures are debuggable.
         * Common SX1262 codes: -2 chip not found / bad SPI, -706 TCXO timeout,
         * -707 SPI cmd timeout, -16 invalid parameter. */
        Serial.printf("[NEXUS] RadioLib begin() = %d "
                      "(freq=%.3f bw=%.1f sf=%u cr=%u tcxo=%.2f)\n",
                      state, freq, bw, (unsigned)sf, (unsigned)cr, tcxo);
        return NX_ERR_IO;
    }

    /* DIO2-as-RF-switch: bare SX1262 modules (Heltec V3, Seeed WIO-SX1262 on
     * XIAO ESP32S3) use DIO2 to drive the chip's internal T/R select.
     * Boards with a discrete external RXEN/TXEN switch (RAK4631, XIAO
     * nRF52840 + WIO-SX1262) set -DNX_LORA_RF_SWITCH_EXTERNAL=1 and install
     * their own RadioLib switch table from main.cpp. Gate on the build flag
     * so the two mechanisms don't fight each other. */
#if !defined(NX_LORA_RF_SWITCH_EXTERNAL) || (NX_LORA_RF_SWITCH_EXTERNAL == 0)
    rl->setDio2AsRfSwitch(true);
#endif

    if (config->crc_on) {
        rl->setCRC(2); /* 2-byte CRC */
    } else {
        rl->setCRC(0);
    }

    if (config->implicit_header) {
        rl->implicitHeader(255);
    } else {
        rl->explicitHeader();
    }

    /* Interrupt-driven RX: wire DIO1 -> ISR, arm continuous receive.
     * The same ISR also fires on TX-done -- rl_transmit() uses the
     * s_rx_flag as a generic "DIO1 fired" signal and re-arms RX
     * afterwards. */
    rl->setPacketReceivedAction(rl_dio1_isr);
    rl_arm_rx(rl);

    memcpy(&radio->config, config, sizeof(nx_lora_config_t));
    radio->state = NX_RADIO_RX;
    LORA_LOG("init OK freq=%.3fMHz bw=%.1fkHz sf=%u cr=%u sw=0x%02X "
             "pwr=%ddBm pre=%u tcxo=%.2fV\n",
             freq, bw, (unsigned)sf, (unsigned)cr, (unsigned)sw,
             (int)power, (unsigned)pre, tcxo);
    return NX_OK;
}

/* ── HAL: transmit ──────────────────────────────────────────────────────
 *
 * Async, cooperative TX. Previously this used RadioLib's synchronous
 * transmit() which blocks the core for the full airtime (~350 ms at
 * SF9). On ESP32-S3 + NimBLE that holds the core long enough for BLE
 * GATT writes to time out, and on long packets risks the task
 * watchdog. The new flow:
 *   1. Park the radio in standby (SX1262 can't TX while RX is armed).
 *   2. startTransmit() arms the radio asynchronously.
 *   3. Yield while polling DIO1 (s_rx_flag is reused as a generic
 *      "DIO1 fired" signal -- TX-done uses the same line).
 *   4. finishTransmit() reads the result code.
 *   5. Re-arm RX so the next packet from a peer doesn't get missed.
 *
 * Transient errors (-706 TCXO timeout, -16 invalid parameter) get a
 * single retry with a short backoff -- a single packet loss used to
 * silently drop the announce / RREQ on the floor.
 */
static nx_err_t rl_transmit(nx_lora_radio_t *radio,
                             const uint8_t *data, size_t len)
{
    SX1262 *rl = get_rl(radio);

    /* Park RX before TX (SX1262 can't do both). */
    if (s_rx_armed) {
        rl->standby();
        s_rx_armed = false;
    }

    radio->state = NX_RADIO_TX;
    int state = RADIOLIB_ERR_NONE;

    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t t0 = millis();

        /* Clear the DIO1 flag and arm async TX. */
        s_rx_flag = false;
        state = rl->startTransmit((uint8_t *)data, len);
        if (state != RADIOLIB_ERR_NONE) {
            LORA_LOG("TX startTransmit err=%d (attempt %d)\n", state, attempt + 1);
            if (attempt + 1 < 2) {
                nx_platform_sleep_ms(20);
                continue;
            }
            break;
        }

        /* Poll DIO1 for TX-done. Cap the wait at twice the calculated
         * airtime plus a fixed slack -- if DIO1 never fires we don't
         * want to wedge the loop forever. */
        uint32_t airtime = nx_lora_airtime_ms(&radio->config, len);
        uint32_t budget = airtime * 2 + 200;
        while (!s_rx_flag) {
            if (millis() - t0 > budget) {
                LORA_LOG("TX timeout after %lu ms (budget=%lu, len=%zu)\n",
                         (unsigned long)(millis() - t0),
                         (unsigned long)budget, len);
                state = RADIOLIB_ERR_TX_TIMEOUT;
                break;
            }
            yield();
            nx_platform_sleep_ms(1);
        }

        if (state == RADIOLIB_ERR_NONE) {
            state = rl->finishTransmit();
            LORA_LOG("TX seq ok len=%zu took=%lums err=%d\n",
                     len, (unsigned long)(millis() - t0), state);
            if (state == RADIOLIB_ERR_NONE) break;
        }

        if (attempt + 1 < 2) {
            nx_platform_sleep_ms(20);
            LORA_LOG("TX retry\n");
        }
    }

    /* Re-arm RX regardless of final TX result. */
    rl_arm_rx(rl);
    radio->state = NX_RADIO_RX;

    if (state == RADIOLIB_ERR_NONE) return NX_OK;
    if (state == RADIOLIB_ERR_TX_TIMEOUT) return NX_ERR_TIMEOUT;
    return NX_ERR_IO;
}

/* ── HAL: receive ─────────────────────────────────────────────────────── */

static nx_err_t rl_receive(nx_lora_radio_t *radio,
                            uint8_t *buf, size_t buf_len,
                            size_t *out_len, nx_lora_rx_info_t *rx_info,
                            uint32_t timeout_ms)
{
    SX1262 *rl = get_rl(radio);

    if (!s_rx_armed) rl_arm_rx(rl);
    radio->state = NX_RADIO_RX;

    /* Non-blocking poll: cooperative yield while we wait for the DIO1 ISR.
     * Loop runs at least once so a flag set between calls is caught. */
    uint32_t t0 = millis();
    do {
        if (s_rx_flag) break;
        if ((millis() - t0) >= timeout_ms) {
            *out_len = 0;
            return NX_ERR_TIMEOUT;
        }
        yield();
        delay(1);
    } while (true);

    s_rx_flag = false;
    size_t plen = rl->getPacketLength();
    if (plen > buf_len) plen = buf_len;

    int state = rl->readData(buf, plen);

    /* Re-arm for the next packet. */
    rl_arm_rx(rl);

    if (state == RADIOLIB_ERR_NONE) {
        *out_len = plen;
        if (rx_info) {
            rx_info->rssi = (int16_t)rl->getRSSI();
            rx_info->snr = (int8_t)rl->getSNR();
            rx_info->freq_error = (uint32_t)rl->getFrequencyError();
        }
        LORA_LOG("RX len=%zu rssi=%d snr=%d\n",
                 plen, (int)rl->getRSSI(), (int)rl->getSNR());
        return NX_OK;
    }

    LORA_LOG("RX readData err=%d (plen=%zu)\n", state, plen);
    *out_len = 0;
    return NX_ERR_IO;
}

/* ── HAL: CAD ─────────────────────────────────────────────────────────── */

static nx_err_t rl_cad(nx_lora_radio_t *radio, bool *activity)
{
    SX1262 *rl = get_rl(radio);

    /* scanChannel() requires STANDBY, not RX.  Park the radio and clear
     * s_rx_armed so lora_recv() will re-arm after this call returns
     * regardless of whether a transmit follows. */
    if (s_rx_armed) {
        rl->standby();
        s_rx_armed = false;
    }

    radio->state = NX_RADIO_CAD;
    int state = rl->scanChannel();
    radio->state = NX_RADIO_STANDBY;

    if (state == RADIOLIB_LORA_DETECTED) {
        *activity = true;
    } else if (state == RADIOLIB_CHANNEL_FREE) {
        *activity = false;
    } else {
        return NX_ERR_IO;
    }
    return NX_OK;
}

/* ── HAL: sleep/standby ───────────────────────────────────────────────── */

static nx_err_t rl_sleep(nx_lora_radio_t *radio)
{
    SX1262 *rl = get_rl(radio);
    s_rx_armed = false;
    int state = rl->sleep(true); /* warm start */
    radio->state = NX_RADIO_SLEEP;
    return (state == RADIOLIB_ERR_NONE) ? NX_OK : NX_ERR_IO;
}

static nx_err_t rl_standby(nx_lora_radio_t *radio)
{
    SX1262 *rl = get_rl(radio);
    s_rx_armed = false;
    int state = rl->standby();
    radio->state = NX_RADIO_STANDBY;
    return (state == RADIOLIB_ERR_NONE) ? NX_OK : NX_ERR_IO;
}

/* ── HAL: reconfigure ─────────────────────────────────────────────────── */

static nx_err_t rl_reconfigure(nx_lora_radio_t *radio,
                                const nx_lora_config_t *config)
{
    SX1262 *rl = get_rl(radio);
    int err = 0;

    if (config->frequency_hz != radio->config.frequency_hz) {
        err |= rl->setFrequency((float)config->frequency_hz / 1e6f);
    }
    if (config->bandwidth_hz != radio->config.bandwidth_hz) {
        err |= rl->setBandwidth((float)config->bandwidth_hz / 1e3f);
    }
    if (config->spreading_factor != radio->config.spreading_factor) {
        err |= rl->setSpreadingFactor(config->spreading_factor);
    }
    if (config->coding_rate != radio->config.coding_rate) {
        err |= rl->setCodingRate(config->coding_rate);
    }
    if (config->tx_power_dbm != radio->config.tx_power_dbm) {
        err |= rl->setOutputPower(config->tx_power_dbm);
    }

    if (err != 0) return NX_ERR_IO;

    memcpy(&radio->config, config, sizeof(nx_lora_config_t));

    /* Re-arm RX so live reconfigure (Android settings UI) keeps listening. */
    rl_arm_rx(rl);
    radio->state = NX_RADIO_RX;
    return NX_OK;
}

/* ── HAL: destroy ─────────────────────────────────────────────────────── */

static void rl_destroy(nx_lora_radio_t *radio)
{
    if (!radio) return;
    free(radio->hw);
    radio->hw = NULL;
}

/* ── Ops vtable ───────────────────────────────────────────────────────── */

static const nx_lora_radio_ops_t radiolib_ops = {
    .init        = rl_init,
    .transmit    = rl_transmit,
    .receive     = rl_receive,
    .cad         = rl_cad,
    .sleep       = rl_sleep,
    .standby     = rl_standby,
    .reconfigure = rl_reconfigure,
    .destroy     = rl_destroy,
};

/* ── Public API ───────────────────────────────────────────────────────── */

nx_lora_radio_t *nx_radiolib_create(SX1262 *radio)
{
    nx_lora_radio_t *r = (nx_lora_radio_t *)calloc(1, sizeof(nx_lora_radio_t));
    if (!r) return NULL;

    radiolib_hw_t *hw = (radiolib_hw_t *)calloc(1, sizeof(radiolib_hw_t));
    if (!hw) { free(r); return NULL; }

    hw->rl = radio;
    r->ops = &radiolib_ops;
    r->hw = hw;
    r->state = NX_RADIO_SLEEP;

    return r;
}

nx_transport_t *nx_radiolib_transport_setup(SX1262 *radio,
                                             const nx_lora_config_t *config)
{
    /* Create the radio HAL */
    nx_lora_radio_t *lora = nx_radiolib_create(radio);
    if (!lora) return NULL;

    /* Initialize the radio */
    if (lora->ops->init(lora, config) != NX_OK) {
        free(lora->hw);
        free(lora);
        return NULL;
    }

    /* Create and register the NEXUS transport */
    nx_transport_t *t = nx_lora_transport_create();
    if (!t) {
        lora->ops->destroy(lora);
        free(lora);
        return NULL;
    }

    /* Init transport with radio pointer */
    if (t->ops->init(t, &lora) != NX_OK) {
        nx_transport_destroy(t);
        lora->ops->destroy(lora);
        free(lora);
        return NULL;
    }

    if (nx_transport_register(t) != NX_OK) {
        nx_transport_destroy(t);
        return NULL;
    }

    return t;
}

