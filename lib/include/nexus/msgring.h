/*
 * NEXUS Protocol -- Inbound Message Ring Buffer
 *
 * Stores the last NX_MSGRING_CAPACITY inbound messages so a BLE client
 * that reconnects after a reboot (or disconnect) can resync without
 * requiring the sender to retransmit.
 *
 * Persistence: serialize/deserialize to a flat byte buffer suitable for
 * NVS (ESP32) or InternalFS (nRF52).
 *
 * Memory budget: 32 × ~258B ≈ 8.3 KB (fits nRF52840 comfortably).
 */
#ifndef NEXUS_MSGRING_H
#define NEXUS_MSGRING_H

#include "types.h"

/* ── Constants ──────────────────────────────────────────────────────── */

#define NX_MSGRING_CAPACITY     32   /* Max stored messages */

/* ── Entry ──────────────────────────────────────────────────────────── */

typedef struct {
    nx_addr_short_t src;                   /* Sender short address        */
    uint32_t        timestamp_s;           /* Unix epoch seconds          */
    uint16_t        len;                   /* Payload length (0..242)     */
    uint8_t         data[NX_MAX_PAYLOAD];  /* Raw payload bytes           */
    bool            valid;
} nx_msgring_entry_t;

/* ── Ring Buffer ────────────────────────────────────────────────────── */

typedef struct {
    nx_msgring_entry_t entries[NX_MSGRING_CAPACITY];
    int head;     /* Next write position (wraps) */
    int count;    /* Entries currently stored (0..CAPACITY) */
} nx_msgring_t;

/* ── API ────────────────────────────────────────────────────────────── */

/* Initialize / clear the ring buffer. */
void nx_msgring_init(nx_msgring_t *ring);

/*
 * Push a message into the ring. Overwrites the oldest entry when full.
 * timestamp_s: caller-provided Unix epoch seconds (nx_platform_time_ms()/1000).
 */
void nx_msgring_push(nx_msgring_t *ring,
                     const nx_addr_short_t *src,
                     uint32_t timestamp_s,
                     const uint8_t *data, size_t len);

/* Number of valid entries (0..CAPACITY). */
int nx_msgring_count(const nx_msgring_t *ring);

/*
 * Get an entry by index from newest (0 = most recent, 1 = second most
 * recent, ...). Returns NULL if idx >= count.
 */
const nx_msgring_entry_t *nx_msgring_get(const nx_msgring_t *ring, int idx);

/* Clear all entries. */
void nx_msgring_clear(nx_msgring_t *ring);

/* ── Persistence ────────────────────────────────────────────────────── */

/*
 * Blob format:
 *   [magic(4)][version(1)][count(1)]
 *   count × [src(4)][timestamp(4 BE)][len(2 BE)][data(len)]
 *
 * Variable-length on disk: only the actual payload bytes of each entry
 * are stored, so a ring with short messages uses much less than worst-case.
 */
#define NX_MSGRING_BLOB_MAGIC    0x4E585242u  /* 'N','X','R','B' */
#define NX_MSGRING_BLOB_VERSION  0x01
#define NX_MSGRING_BLOB_HEADER   6            /* magic(4)+version(1)+count(1) */

/* Worst-case blob size: header + CAPACITY*(4+4+2+242) */
#define NX_MSGRING_BLOB_MAX \
    (NX_MSGRING_BLOB_HEADER + NX_MSGRING_CAPACITY * (4 + 4 + 2 + NX_MAX_PAYLOAD))

/*
 * Serialize all valid entries (oldest first) into buf.
 * On success *out_len holds bytes written.
 */
nx_err_t nx_msgring_serialize(const nx_msgring_t *ring,
                              uint8_t *buf, size_t buf_cap,
                              size_t *out_len);

/*
 * Deserialize a previously serialized blob into ring.
 * The ring is fully re-initialised first. Rejects unknown magic/version.
 */
nx_err_t nx_msgring_deserialize(nx_msgring_t *ring,
                                const uint8_t *buf, size_t len);

#endif /* NEXUS_MSGRING_H */
