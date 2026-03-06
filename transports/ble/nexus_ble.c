/*
 * NEXUS Protocol -- BLE Transport
 *
 * Uses the BLE radio HAL abstraction for hardware independence.
 * BLE GATT PDUs are self-delimiting so no framing is needed.
 * No duty cycle or CAD (BLE stack handles these).
 */
#include "nexus/transport.h"
#include "nexus/ble_radio.h"
#include "nexus/platform.h"

#include <string.h>

/* ── BLE Transport State ─────────────────────────────────────────────── */

typedef struct {
    nx_ble_radio_t *radio;
} ble_state_t;

/* ── Transport vtable ────────────────────────────────────────────────── */

static nx_err_t ble_init(nx_transport_t *t, const void *config)
{
    const nx_ble_radio_t *radio_in = *(nx_ble_radio_t *const *)config;
    if (!radio_in) return NX_ERR_INVALID_ARG;

    ble_state_t *s = (ble_state_t *)nx_platform_alloc(sizeof(ble_state_t));
    if (!s) return NX_ERR_NO_MEMORY;
    memset(s, 0, sizeof(*s));

    s->radio = (nx_ble_radio_t *)radio_in;

    t->state  = s;
    t->active = true;
    return NX_OK;
}

static nx_err_t ble_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    ble_state_t *s = (ble_state_t *)t->state;
    if (!s || !s->radio) return NX_ERR_TRANSPORT;
    if (len > NX_MAX_PACKET) return NX_ERR_INVALID_ARG;

    return s->radio->ops->send(s->radio, data, len);
}

static nx_err_t ble_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                          size_t *out_len, uint32_t timeout_ms)
{
    ble_state_t *s = (ble_state_t *)t->state;
    if (!s || !s->radio) return NX_ERR_TRANSPORT;

    return s->radio->ops->recv(s->radio, buf, buf_len, out_len, timeout_ms);
}

static void ble_destroy(nx_transport_t *t)
{
    ble_state_t *s = (ble_state_t *)t->state;
    if (s) {
        if (s->radio && s->radio->ops->sleep) {
            s->radio->ops->sleep(s->radio);
        }
        nx_platform_free(s);
    }
    t->state  = NULL;
    t->active = false;
}

static const nx_transport_ops_t ble_ops = {
    .init    = ble_init,
    .send    = ble_send,
    .recv    = ble_recv,
    .destroy = ble_destroy,
};

nx_transport_t *nx_ble_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));
    t->type   = NX_TRANSPORT_BLE;
    t->name   = "ble";
    t->ops    = &ble_ops;
    t->active = false;
    return t;
}

/* ── Mock BLE Radio Implementation (desktop testing) ─────────────────── */

#define MOCK_RING_SIZE  16
#define MOCK_MAX_PKT    256

typedef struct {
    uint8_t  data[MOCK_MAX_PKT];
    size_t   len;
} mock_ble_pkt_t;

typedef struct mock_ble_state mock_ble_state_t;

struct mock_ble_state {
    mock_ble_pkt_t       ring[MOCK_RING_SIZE];
    int                  head;
    int                  tail;
    int                  count;
    mock_ble_state_t    *peer;
    nx_ble_config_t      config;
};

static nx_err_t mock_ble_init(nx_ble_radio_t *radio,
                               const nx_ble_config_t *config)
{
    mock_ble_state_t *ms = (mock_ble_state_t *)radio->hw;
    if (config) {
        ms->config = *config;
        radio->config = *config;
    }
    radio->state = radio->config.peripheral ? NX_BLE_ADVERTISING : NX_BLE_SCANNING;
    return NX_OK;
}

static nx_err_t mock_ble_send(nx_ble_radio_t *radio,
                               const uint8_t *data, size_t len)
{
    mock_ble_state_t *ms = (mock_ble_state_t *)radio->hw;
    if (!ms->peer) return NX_ERR_TRANSPORT;
    if (len > MOCK_MAX_PKT) return NX_ERR_INVALID_ARG;

    mock_ble_state_t *peer = ms->peer;
    if (peer->count >= MOCK_RING_SIZE) return NX_ERR_FULL;

    mock_ble_pkt_t *slot = &peer->ring[peer->tail];
    memcpy(slot->data, data, len);
    slot->len = len;
    peer->tail = (peer->tail + 1) % MOCK_RING_SIZE;
    peer->count++;

    return NX_OK;
}

static nx_err_t mock_ble_recv(nx_ble_radio_t *radio,
                               uint8_t *buf, size_t buf_len,
                               size_t *out_len, uint32_t timeout_ms)
{
    mock_ble_state_t *ms = (mock_ble_state_t *)radio->hw;
    uint64_t deadline = nx_platform_time_ms() + timeout_ms;

    while (ms->count == 0) {
        if (nx_platform_time_ms() >= deadline) return NX_ERR_TIMEOUT;
    }

    mock_ble_pkt_t *slot = &ms->ring[ms->head];
    if (slot->len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(buf, slot->data, slot->len);
    *out_len = slot->len;
    ms->head = (ms->head + 1) % MOCK_RING_SIZE;
    ms->count--;

    return NX_OK;
}

static nx_err_t mock_ble_sleep(nx_ble_radio_t *radio)
{
    radio->state = NX_BLE_IDLE;
    return NX_OK;
}

static void mock_ble_destroy(nx_ble_radio_t *radio)
{
    if (radio->hw) {
        mock_ble_state_t *ms = (mock_ble_state_t *)radio->hw;
        if (ms->peer) ms->peer->peer = NULL;
        nx_platform_free(ms);
        radio->hw = NULL;
    }
}

static const nx_ble_radio_ops_t mock_ble_radio_ops = {
    .init    = mock_ble_init,
    .send    = mock_ble_send,
    .recv    = mock_ble_recv,
    .sleep   = mock_ble_sleep,
    .destroy = mock_ble_destroy,
};

nx_ble_radio_t *nx_ble_mock_create(void)
{
    nx_ble_radio_t *radio = (nx_ble_radio_t *)nx_platform_alloc(
        sizeof(nx_ble_radio_t));
    if (!radio) return NULL;

    mock_ble_state_t *ms = (mock_ble_state_t *)nx_platform_alloc(
        sizeof(mock_ble_state_t));
    if (!ms) {
        nx_platform_free(radio);
        return NULL;
    }

    memset(radio, 0, sizeof(*radio));
    memset(ms, 0, sizeof(*ms));

    radio->ops   = &mock_ble_radio_ops;
    radio->hw    = ms;
    radio->state = NX_BLE_IDLE;

    return radio;
}

void nx_ble_mock_link(nx_ble_radio_t *a, nx_ble_radio_t *b)
{
    if (!a || !b) return;
    mock_ble_state_t *ma = (mock_ble_state_t *)a->hw;
    mock_ble_state_t *mb = (mock_ble_state_t *)b->hw;
    ma->peer = mb;
    mb->peer = ma;
}
