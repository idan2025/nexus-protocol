/*
 * NEXUS Protocol -- LoRa Transport
 *
 * Uses the radio HAL abstraction for hardware independence.
 * Handles CAD-based collision avoidance, duty cycle tracking,
 * and random backoff.
 *
 * LoRa packets go directly on-air (no framing delimiters needed --
 * the radio handles packet boundaries via preamble + sync word + length).
 */
#include "nexus/transport.h"
#include "nexus/lora_radio.h"
#include "nexus/platform.h"

#include <string.h>

/* ── LoRa Transport Config ───────────────────────────────────────────── */

/* Max retries on CAD-busy */
#define LORA_MAX_CAD_RETRIES   5

/* Backoff range (ms) */
#define LORA_BACKOFF_MIN_MS   20
#define LORA_BACKOFF_MAX_MS  200

/* Duty cycle: max fraction of time transmitting (1% = 100) */
#define LORA_DUTY_CYCLE_DENOM 100

typedef struct {
    nx_lora_radio_t *radio;
    uint64_t         tx_airtime_sum_ms;  /* Total TX airtime in current window */
    uint64_t         window_start_ms;    /* Start of duty cycle window */
    int16_t          last_rssi;
    int8_t           last_snr;
} lora_state_t;

/* ── Random backoff helper ───────────────────────────────────────────── */

static uint32_t random_backoff_ms(void)
{
    uint16_t r;
    nx_platform_random((uint8_t *)&r, sizeof(r));
    uint32_t range = LORA_BACKOFF_MAX_MS - LORA_BACKOFF_MIN_MS;
    return LORA_BACKOFF_MIN_MS + (r % range);
}

/* Busy-wait for ms (no OS sleep on bare-metal) */
static void delay_ms(uint32_t ms)
{
    uint64_t start = nx_platform_time_ms();
    while (nx_platform_time_ms() - start < ms) {
        /* spin */
    }
}

/* ── Duty Cycle ──────────────────────────────────────────────────────── */

#define DUTY_WINDOW_MS  60000  /* 1-minute window */

static bool duty_cycle_ok(lora_state_t *s, uint32_t airtime_ms)
{
    uint64_t now = nx_platform_time_ms();

    /* Reset window if expired */
    if (now - s->window_start_ms > DUTY_WINDOW_MS) {
        s->window_start_ms = now;
        s->tx_airtime_sum_ms = 0;
    }

    uint64_t max_airtime = DUTY_WINDOW_MS / LORA_DUTY_CYCLE_DENOM;
    return (s->tx_airtime_sum_ms + airtime_ms) <= max_airtime;
}

/* ── Airtime Calculation ─────────────────────────────────────────────── */

uint32_t nx_lora_airtime_ms(const nx_lora_config_t *config, size_t payload_len)
{
    if (!config || config->bandwidth_hz == 0) return 0;

    uint8_t sf = config->spreading_factor;
    uint8_t cr = config->coding_rate;
    uint16_t preamble = config->preamble_len;
    bool de = (sf >= 11 && config->bandwidth_hz <= 125000); /* Low data rate opt */
    bool ih = config->implicit_header;
    bool crc = config->crc_on;

    /* Symbol duration in microseconds */
    /* T_sym = 2^SF / BW (in Hz) * 1e6 us */
    uint32_t t_sym_us = ((uint32_t)1 << sf) * 1000000 / config->bandwidth_hz;

    /* Preamble time */
    uint32_t t_preamble_us = (preamble + (uint32_t)4) * t_sym_us +
                             t_sym_us / 4; /* 4.25 symbols */

    /* Payload symbol count (LoRa formula) */
    int pl = (int)payload_len;
    int num = 8 * pl - 4 * sf + 28 + (crc ? 16 : 0) - (ih ? 20 : 0);
    if (num < 0) num = 0;
    int denom = 4 * (sf - (de ? 2 : 0));
    if (denom <= 0) denom = 1;
    int n_payload = 8 + ((num + denom - 1) / denom) * (cr);

    uint32_t t_payload_us = (uint32_t)n_payload * t_sym_us;
    uint32_t total_us = t_preamble_us + t_payload_us;

    return (total_us + 999) / 1000; /* Round up to ms */
}

/* ── Transport vtable ────────────────────────────────────────────────── */

static nx_err_t lora_init(nx_transport_t *t, const void *config)
{
    const nx_lora_radio_t *radio_in = *(nx_lora_radio_t *const *)config;
    if (!radio_in) return NX_ERR_INVALID_ARG;

    lora_state_t *s = (lora_state_t *)nx_platform_alloc(sizeof(lora_state_t));
    if (!s) return NX_ERR_NO_MEMORY;
    memset(s, 0, sizeof(*s));

    /* The radio should already be initialized by the firmware */
    s->radio = (nx_lora_radio_t *)radio_in;
    s->window_start_ms = nx_platform_time_ms();

    t->state  = s;
    t->active = true;
    return NX_OK;
}

static nx_err_t lora_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    lora_state_t *s = (lora_state_t *)t->state;
    if (!s || !s->radio) return NX_ERR_TRANSPORT;
    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    /* Check duty cycle */
    uint32_t airtime = nx_lora_airtime_ms(&s->radio->config, len);
    if (!duty_cycle_ok(s, airtime)) return NX_ERR_FULL;

    /* CAD-based collision avoidance */
    for (int retry = 0; retry < LORA_MAX_CAD_RETRIES; retry++) {
        if (s->radio->ops->cad) {
            bool activity = false;
            nx_err_t err = s->radio->ops->cad(s->radio, &activity);
            if (err != NX_OK) return err;
            if (activity) {
                /* Channel busy -- random backoff */
                delay_ms(random_backoff_ms());
                continue;
            }
        }
        /* Channel clear -- transmit */
        break;
    }

    nx_err_t err = s->radio->ops->transmit(s->radio, data, len);
    if (err == NX_OK) {
        s->tx_airtime_sum_ms += airtime;
    }
    return err;
}

static nx_err_t lora_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                          size_t *out_len, uint32_t timeout_ms)
{
    lora_state_t *s = (lora_state_t *)t->state;
    if (!s || !s->radio) return NX_ERR_TRANSPORT;

    nx_lora_rx_info_t rx_info;
    memset(&rx_info, 0, sizeof(rx_info));

    nx_err_t err = s->radio->ops->receive(s->radio, buf, buf_len,
                                          out_len, &rx_info, timeout_ms);
    if (err == NX_OK) {
        s->last_rssi = rx_info.rssi;
        s->last_snr  = rx_info.snr;
    }
    return err;
}

static void lora_destroy(nx_transport_t *t)
{
    lora_state_t *s = (lora_state_t *)t->state;
    if (s) {
        if (s->radio && s->radio->ops->sleep) {
            s->radio->ops->sleep(s->radio);
        }
        nx_platform_free(s);
    }
    t->state  = NULL;
    t->active = false;
}

static const nx_transport_ops_t lora_ops = {
    .init    = lora_init,
    .send    = lora_send,
    .recv    = lora_recv,
    .destroy = lora_destroy,
};

nx_transport_t *nx_lora_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_LORA;
    t->name   = "lora";
    t->ops    = &lora_ops;
    t->active = false;
    return t;
}

/* ── Mock Radio Implementation (desktop testing) ─────────────────────── */

#define MOCK_RING_SIZE  16
#define MOCK_MAX_PKT    256

typedef struct {
    uint8_t  data[MOCK_MAX_PKT];
    size_t   len;
} mock_pkt_t;

typedef struct {
    mock_pkt_t     ring[MOCK_RING_SIZE];
    int            head;
    int            tail;
    int            count;
    struct mock_radio_state *peer; /* Linked peer for loopback */
    nx_lora_config_t config;
} mock_radio_state_t;

/* Forward declare for the struct self-reference */
struct mock_radio_state {
    mock_pkt_t     ring[MOCK_RING_SIZE];
    int            head;
    int            tail;
    int            count;
    struct mock_radio_state *peer;
    nx_lora_config_t config;
};

static nx_err_t mock_init(nx_lora_radio_t *radio, const nx_lora_config_t *config)
{
    struct mock_radio_state *ms = (struct mock_radio_state *)radio->hw;
    if (config) {
        ms->config = *config;
        radio->config = *config;
    }
    radio->state = NX_RADIO_STANDBY;
    return NX_OK;
}

static nx_err_t mock_transmit(nx_lora_radio_t *radio,
                              const uint8_t *data, size_t len)
{
    struct mock_radio_state *ms = (struct mock_radio_state *)radio->hw;
    if (!ms->peer) return NX_ERR_TRANSPORT; /* No linked peer */
    if (len > MOCK_MAX_PKT) return NX_ERR_INVALID_ARG;

    struct mock_radio_state *peer = ms->peer;
    if (peer->count >= MOCK_RING_SIZE) return NX_ERR_FULL;

    mock_pkt_t *slot = &peer->ring[peer->tail];
    memcpy(slot->data, data, len);
    slot->len = len;
    peer->tail = (peer->tail + 1) % MOCK_RING_SIZE;
    peer->count++;

    return NX_OK;
}

static nx_err_t mock_receive(nx_lora_radio_t *radio,
                             uint8_t *buf, size_t buf_len,
                             size_t *out_len, nx_lora_rx_info_t *rx_info,
                             uint32_t timeout_ms)
{
    struct mock_radio_state *ms = (struct mock_radio_state *)radio->hw;
    uint64_t deadline = nx_platform_time_ms() + timeout_ms;

    while (ms->count == 0) {
        if (nx_platform_time_ms() >= deadline) return NX_ERR_TIMEOUT;
        /* Busy-wait in mock (no OS sleep needed) */
    }

    mock_pkt_t *slot = &ms->ring[ms->head];
    if (slot->len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(buf, slot->data, slot->len);
    *out_len = slot->len;
    ms->head = (ms->head + 1) % MOCK_RING_SIZE;
    ms->count--;

    if (rx_info) {
        rx_info->rssi = -50;  /* Simulated values */
        rx_info->snr  = 10;
        rx_info->freq_error = 0;
    }
    return NX_OK;
}

static nx_err_t mock_cad(nx_lora_radio_t *radio, bool *activity)
{
    (void)radio;
    *activity = false; /* Mock: channel always clear */
    return NX_OK;
}

static nx_err_t mock_sleep(nx_lora_radio_t *radio)
{
    radio->state = NX_RADIO_SLEEP;
    return NX_OK;
}

static nx_err_t mock_standby(nx_lora_radio_t *radio)
{
    radio->state = NX_RADIO_STANDBY;
    return NX_OK;
}

static nx_err_t mock_reconfigure(nx_lora_radio_t *radio,
                                 const nx_lora_config_t *config)
{
    if (config) radio->config = *config;
    return NX_OK;
}

static void mock_destroy(nx_lora_radio_t *radio)
{
    if (radio->hw) {
        struct mock_radio_state *ms = (struct mock_radio_state *)radio->hw;
        /* Unlink peer */
        if (ms->peer) ms->peer->peer = NULL;
        nx_platform_free(ms);
        radio->hw = NULL;
    }
}

static const nx_lora_radio_ops_t mock_radio_ops = {
    .init        = mock_init,
    .transmit    = mock_transmit,
    .receive     = mock_receive,
    .cad         = mock_cad,
    .sleep       = mock_sleep,
    .standby     = mock_standby,
    .reconfigure = mock_reconfigure,
    .destroy     = mock_destroy,
};

nx_lora_radio_t *nx_lora_mock_create(void)
{
    nx_lora_radio_t *radio = (nx_lora_radio_t *)nx_platform_alloc(
        sizeof(nx_lora_radio_t));
    if (!radio) return NULL;

    struct mock_radio_state *ms = (struct mock_radio_state *)nx_platform_alloc(
        sizeof(struct mock_radio_state));
    if (!ms) {
        nx_platform_free(radio);
        return NULL;
    }

    memset(radio, 0, sizeof(*radio));
    memset(ms, 0, sizeof(*ms));

    radio->ops   = &mock_radio_ops;
    radio->hw    = ms;
    radio->state = NX_RADIO_SLEEP;

    return radio;
}

void nx_lora_mock_link(nx_lora_radio_t *a, nx_lora_radio_t *b)
{
    if (!a || !b) return;
    struct mock_radio_state *ma = (struct mock_radio_state *)a->hw;
    struct mock_radio_state *mb = (struct mock_radio_state *)b->hw;
    ma->peer = mb;
    mb->peer = ma;
}
