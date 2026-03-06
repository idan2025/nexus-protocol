/*
 * NEXUS Firmware -- RadioLib HAL Bridge
 *
 * Maps nx_lora_radio_ops_t to RadioLib's SX1262 C++ API.
 */
#include "radiolib_hal.h"
#include <string.h>

extern "C" {
#include "nexus/platform.h"
}

/* ── Internal state ───────────────────────────────────────────────────── */

typedef struct {
    SX1262 *rl;   /* RadioLib SX1262 instance (not owned) */
} radiolib_hw_t;

static inline SX1262 *get_rl(nx_lora_radio_t *radio)
{
    return ((radiolib_hw_t *)radio->hw)->rl;
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

    int state = rl->begin(freq, bw, sf, cr, sw, power, pre, 0);
    if (state != RADIOLIB_ERR_NONE) return NX_ERR_IO;

    /* SX1262 requires DIO2 as RF switch on most boards */
    rl->setDio2AsRfSwitch(true);

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

    memcpy(&radio->config, config, sizeof(nx_lora_config_t));
    radio->state = NX_RADIO_STANDBY;
    return NX_OK;
}

/* ── HAL: transmit ────────────────────────────────────────────────────── */

static nx_err_t rl_transmit(nx_lora_radio_t *radio,
                             const uint8_t *data, size_t len)
{
    SX1262 *rl = get_rl(radio);

    radio->state = NX_RADIO_TX;
    int state = rl->transmit((uint8_t *)data, len);
    radio->state = NX_RADIO_STANDBY;

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

    radio->state = NX_RADIO_RX;

    /* Start receiving with timeout */
    int state = rl->receive(buf, buf_len);

    radio->state = NX_RADIO_STANDBY;

    if (state == RADIOLIB_ERR_NONE) {
        *out_len = rl->getPacketLength();
        if (rx_info) {
            rx_info->rssi = (int16_t)rl->getRSSI();
            rx_info->snr = (int8_t)rl->getSNR();
            rx_info->freq_error = (uint32_t)rl->getFrequencyError();
        }
        return NX_OK;
    }

    if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        *out_len = 0;
        return NX_ERR_TIMEOUT;
    }

    *out_len = 0;
    return NX_ERR_IO;
}

/* ── HAL: CAD ─────────────────────────────────────────────────────────── */

static nx_err_t rl_cad(nx_lora_radio_t *radio, bool *activity)
{
    SX1262 *rl = get_rl(radio);

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
    int state = rl->sleep(true); /* warm start */
    radio->state = NX_RADIO_SLEEP;
    return (state == RADIOLIB_ERR_NONE) ? NX_OK : NX_ERR_IO;
}

static nx_err_t rl_standby(nx_lora_radio_t *radio)
{
    SX1262 *rl = get_rl(radio);
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

extern "C" {

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

} /* extern "C" */
