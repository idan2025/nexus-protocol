/*
 * NEXUS Protocol -- Inbound Message Ring Buffer
 */
#include "nexus/msgring.h"
#include <string.h>

/* ── Lifecycle ──────────────────────────────────────────────────────── */

void nx_msgring_init(nx_msgring_t *ring)
{
    if (!ring) return;
    memset(ring, 0, sizeof(*ring));
}

/* ── Push / Get / Count ─────────────────────────────────────────────── */

void nx_msgring_push(nx_msgring_t *ring,
                     const nx_addr_short_t *src,
                     uint32_t timestamp_s,
                     const uint8_t *data, size_t len)
{
    if (!ring || !src || !data || len == 0) return;
    if (len > NX_MAX_PAYLOAD) len = NX_MAX_PAYLOAD;

    nx_msgring_entry_t *e = &ring->entries[ring->head];
    e->src         = *src;
    e->timestamp_s = timestamp_s;
    e->len         = (uint16_t)len;
    memcpy(e->data, data, len);
    e->valid       = true;

    ring->head = (ring->head + 1) % NX_MSGRING_CAPACITY;
    if (ring->count < NX_MSGRING_CAPACITY)
        ring->count++;
}

int nx_msgring_count(const nx_msgring_t *ring)
{
    return ring ? ring->count : 0;
}

const nx_msgring_entry_t *nx_msgring_get(const nx_msgring_t *ring, int idx)
{
    if (!ring || idx < 0 || idx >= ring->count) return NULL;

    /* head points to the next write slot.
     * Most recent entry is at (head - 1), second most recent at (head - 2), etc. */
    int pos = (ring->head - 1 - idx + NX_MSGRING_CAPACITY * 2) % NX_MSGRING_CAPACITY;
    const nx_msgring_entry_t *e = &ring->entries[pos];
    return e->valid ? e : NULL;
}

void nx_msgring_clear(nx_msgring_t *ring)
{
    if (!ring) return;
    nx_msgring_init(ring);
}

/* ── Persistence helpers ────────────────────────────────────────────── */

static void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/* ── Serialize ──────────────────────────────────────────────────────── */

nx_err_t nx_msgring_serialize(const nx_msgring_t *ring,
                              uint8_t *buf, size_t buf_cap,
                              size_t *out_len)
{
    if (!ring || !buf || !out_len) return NX_ERR_INVALID_ARG;

    /* Calculate required size */
    size_t need = NX_MSGRING_BLOB_HEADER;
    int n = ring->count;

    /* Walk entries oldest-first to compute total size */
    for (int i = n - 1; i >= 0; i--) {
        const nx_msgring_entry_t *e = nx_msgring_get(ring, i);
        if (!e) continue;
        need += 4 + 4 + 2 + e->len; /* src + ts + len + data */
    }

    if (buf_cap < need) return NX_ERR_BUFFER_TOO_SMALL;

    uint8_t *p = buf;

    /* Header */
    write_u32_be(p, NX_MSGRING_BLOB_MAGIC); p += 4;
    *p++ = NX_MSGRING_BLOB_VERSION;
    *p++ = (uint8_t)n;

    /* Entries oldest-first (index n-1 is oldest, 0 is newest) */
    for (int i = n - 1; i >= 0; i--) {
        const nx_msgring_entry_t *e = nx_msgring_get(ring, i);
        if (!e) continue;

        memcpy(p, e->src.bytes, 4); p += 4;
        write_u32_be(p, e->timestamp_s); p += 4;
        write_u16_be(p, e->len); p += 2;
        memcpy(p, e->data, e->len); p += e->len;
    }

    *out_len = (size_t)(p - buf);
    return NX_OK;
}

/* ── Deserialize ────────────────────────────────────────────────────── */

nx_err_t nx_msgring_deserialize(nx_msgring_t *ring,
                                const uint8_t *buf, size_t len)
{
    if (!ring || !buf) return NX_ERR_INVALID_ARG;

    nx_msgring_init(ring);

    if (len < NX_MSGRING_BLOB_HEADER) return NX_ERR_BUFFER_TOO_SMALL;

    const uint8_t *p = buf;

    uint32_t magic = read_u32_be(p); p += 4;
    if (magic != NX_MSGRING_BLOB_MAGIC) return NX_ERR_INVALID_ARG;

    uint8_t version = *p++;
    if (version != NX_MSGRING_BLOB_VERSION) return NX_ERR_INVALID_ARG;

    uint8_t count = *p++;
    if (count > NX_MSGRING_CAPACITY) return NX_ERR_INVALID_ARG;

    const uint8_t *end = buf + len;

    for (int i = 0; i < count; i++) {
        if (p + 10 > end) {  /* src(4)+ts(4)+len(2) minimum */
            nx_msgring_init(ring);
            return NX_ERR_BUFFER_TOO_SMALL;
        }

        nx_addr_short_t src;
        memcpy(src.bytes, p, 4); p += 4;

        uint32_t ts = read_u32_be(p); p += 4;
        uint16_t dlen = read_u16_be(p); p += 2;

        if (dlen > NX_MAX_PAYLOAD || p + dlen > end) {
            nx_msgring_init(ring);
            return NX_ERR_INVALID_ARG;
        }

        nx_msgring_push(ring, &src, ts, p, dlen);
        p += dlen;
    }

    return NX_OK;
}
