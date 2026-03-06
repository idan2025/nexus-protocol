/*
 * NEXUS Protocol -- Pipe Transport (In-Memory)
 *
 * Ring-buffer pipe transport for testing. Two pipe transports can be
 * linked so that one's send ring is the other's recv ring and vice versa.
 * Uses a multi-slot ring buffer (16 packets) to prevent data loss when
 * transmit_all() sends on multiple transports during a single poll cycle.
 */
#include "nexus/transport.h"
#include "nexus/platform.h"

#include <string.h>

#define NX_PIPE_BUF_SIZE  512
#define NX_PIPE_RING_SIZE  16

typedef struct {
    uint8_t data[NX_PIPE_BUF_SIZE];
    size_t  len;
} nx_pipe_pkt_t;

typedef struct {
    nx_pipe_pkt_t pkts[NX_PIPE_RING_SIZE];
    int           head;   /* Next write position */
    int           tail;   /* Next read position */
    int           count;  /* Number of queued packets */
} nx_pipe_ring_t;

typedef struct {
    nx_pipe_ring_t *send_ring;  /* Where we write (peer's recv) */
    nx_pipe_ring_t *recv_ring;  /* Where we read (our own recv) */
    nx_pipe_ring_t  own_ring;   /* Our receive ring buffer */
} nx_pipe_state_t;

/* ── Ring buffer helpers ─────────────────────────────────────────────── */

static bool ring_push(nx_pipe_ring_t *r, const uint8_t *data, size_t len)
{
    if (r->count >= NX_PIPE_RING_SIZE || len > NX_PIPE_BUF_SIZE)
        return false;
    memcpy(r->pkts[r->head].data, data, len);
    r->pkts[r->head].len = len;
    r->head = (r->head + 1) % NX_PIPE_RING_SIZE;
    r->count++;
    return true;
}

static bool ring_pop(nx_pipe_ring_t *r, uint8_t *buf, size_t buf_len,
                     size_t *out_len)
{
    if (r->count == 0)
        return false;
    nx_pipe_pkt_t *pkt = &r->pkts[r->tail];
    if (pkt->len > buf_len)
        return false;
    memcpy(buf, pkt->data, pkt->len);
    *out_len = pkt->len;
    r->tail = (r->tail + 1) % NX_PIPE_RING_SIZE;
    r->count--;
    return true;
}

/* ── Ops ─────────────────────────────────────────────────────────────── */

static nx_err_t pipe_init(nx_transport_t *t, const void *config)
{
    (void)config;
    t->active = true;
    return NX_OK;
}

static nx_err_t pipe_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    nx_pipe_state_t *ps = (nx_pipe_state_t *)t->state;
    if (!ring_push(ps->send_ring, data, len))
        return NX_ERR_FULL;
    return NX_OK;
}

static nx_err_t pipe_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    nx_pipe_state_t *ps = (nx_pipe_state_t *)t->state;
    if (!ring_pop(ps->recv_ring, buf, buf_len, out_len)) {
        *out_len = 0;
        return NX_ERR_TIMEOUT;
    }
    return NX_OK;
}

static void pipe_destroy(nx_transport_t *t)
{
    if (t) {
        nx_platform_free(t->state);
        nx_platform_free(t);
    }
}

static const nx_transport_ops_t pipe_ops = {
    .init    = pipe_init,
    .send    = pipe_send,
    .recv    = pipe_recv,
    .destroy = pipe_destroy,
};

/* ── Factory ─────────────────────────────────────────────────────────── */

nx_transport_t *nx_pipe_transport_create(void)
{
    nx_transport_t *t = nx_platform_alloc(sizeof(nx_transport_t));
    if (!t) return NULL;

    nx_pipe_state_t *ps = nx_platform_alloc(sizeof(nx_pipe_state_t));
    if (!ps) {
        nx_platform_free(t);
        return NULL;
    }

    memset(ps, 0, sizeof(*ps));
    ps->send_ring = &ps->own_ring;
    ps->recv_ring = &ps->own_ring;

    t->type   = NX_TRANSPORT_PIPE;
    t->name   = "pipe";
    t->ops    = &pipe_ops;
    t->state  = ps;
    t->active = false;
    t->domain_id = 0;

    return t;
}

void nx_pipe_transport_link(nx_transport_t *a, nx_transport_t *b)
{
    nx_pipe_state_t *sa = (nx_pipe_state_t *)a->state;
    nx_pipe_state_t *sb = (nx_pipe_state_t *)b->state;

    /* Cross-wire: A sends into B's recv ring, B sends into A's recv ring */
    sa->send_ring = &sb->own_ring;
    sa->recv_ring = &sa->own_ring;
    sb->send_ring = &sa->own_ring;
    sb->recv_ring = &sb->own_ring;

    a->active = true;
    b->active = true;

    a->ops->init(a, NULL);
    b->ops->init(b, NULL);
}
