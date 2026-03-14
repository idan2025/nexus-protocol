/*
 * NEXUS Protocol -- Message Fragmentation & Reassembly
 *
 * Large messages (> NX_MAX_PAYLOAD - overhead) are split into up to 16
 * fragments. Each fragment is a normal packet with:
 *   - NX_FLAG_FRAG set in flags (except last fragment)
 *   - NX_FLAG_EXTHDR set (extended header present)
 *   - Extended header: [type(1)][frag_id(2)][idx_total(1)] = 4 bytes prepended to payload
 *     type = NX_EXTHDR_FRAGMENT (0x01)
 *     idx_total: upper nibble = fragment index (0-15), lower nibble = total count (1-16)
 *
 * Fragment payload capacity = NX_MAX_PAYLOAD - NX_FRAG_EXTHDR_SIZE = 238 bytes
 * Max message size = 16 * 238 = 3808 bytes
 *
 * Reassembly collects fragments keyed by (src_addr, frag_id). Incomplete
 * reassemblies expire after a timeout.
 */
#ifndef NEXUS_FRAGMENT_H
#define NEXUS_FRAGMENT_H

#include "types.h"

/* ── Constants ───────────────────────────────────────────────────────── */
#define NX_FRAG_EXTHDR_SIZE     4     /* type(1) + frag_id(2) + idx_total(1) */
#define NX_FRAG_MAX_COUNT      16
#define NX_FRAG_PAYLOAD_CAP    (NX_MAX_PAYLOAD - NX_FRAG_EXTHDR_SIZE) /* 239 */
#define NX_FRAG_MAX_MESSAGE    (NX_FRAG_MAX_COUNT * NX_FRAG_PAYLOAD_CAP) /* 3824 */
#ifndef NX_FRAG_REASSEMBLY_SLOTS
#define NX_FRAG_REASSEMBLY_SLOTS  8
#endif
#define NX_FRAG_TIMEOUT_MS    30000   /* 30s to collect all fragments */

/* ── Fragment Extended Header ────────────────────────────────────────── */

typedef struct {
    uint16_t frag_id;       /* Fragment group identifier */
    uint8_t  frag_index;    /* 0-15: this fragment's index */
    uint8_t  frag_total;    /* 1-16: total fragment count */
} nx_frag_header_t;

/* ── Reassembly Slot ─────────────────────────────────────────────────── */

typedef struct {
    nx_addr_short_t src;
    uint16_t        frag_id;
    uint8_t         frag_total;
    uint16_t        received_mask;  /* Bitmask of received fragment indices */
    uint8_t         data[NX_FRAG_MAX_MESSAGE];
    size_t          frag_sizes[NX_FRAG_MAX_COUNT]; /* Size of each fragment */
    uint64_t        started_ms;
    bool            valid;
} nx_reassembly_t;

/* ── Reassembly Buffer ───────────────────────────────────────────────── */

typedef struct {
    nx_reassembly_t slots[NX_FRAG_REASSEMBLY_SLOTS];
    uint16_t        next_frag_id;
} nx_frag_buffer_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize the fragment buffer. */
void nx_frag_init(nx_frag_buffer_t *fb);

/* Expire incomplete reassemblies. */
void nx_frag_expire(nx_frag_buffer_t *fb, uint64_t now_ms);

/*
 * Fragment a message into packets.
 *
 * Splits data into fragments, writing complete packets (with headers) into
 * the out_pkts array. Sets NX_FLAG_FRAG on all but the last fragment.
 *
 * base_hdr: template header (dst, src, prio, rtype will be copied; flags
 *           and payload_len are set by this function)
 * data/data_len: the full message to fragment
 * out_pkts: array of at least NX_FRAG_MAX_COUNT packets
 * out_count: set to number of fragments produced
 *
 * Returns NX_OK or NX_ERR_BUFFER_TOO_SMALL if message exceeds max.
 */
nx_err_t nx_frag_split(nx_frag_buffer_t *fb,
                       const nx_header_t *base_hdr,
                       const uint8_t *data, size_t data_len,
                       nx_packet_t *out_pkts, int *out_count);

/*
 * Process an incoming fragment.
 *
 * Extracts the fragment extended header, stores the fragment data in the
 * reassembly buffer. When all fragments are received, copies the complete
 * message into out_data.
 *
 * Returns:
 *   NX_OK with *out_len > 0  -- complete message reassembled
 *   NX_OK with *out_len == 0 -- fragment stored, waiting for more
 *   NX_ERR_*                 -- error
 */
nx_err_t nx_frag_receive(nx_frag_buffer_t *fb,
                         const nx_packet_t *pkt,
                         uint8_t *out_data, size_t out_buf_len,
                         size_t *out_len, uint64_t now_ms);

/*
 * Encode a fragment extended header into a buffer.
 * Returns NX_FRAG_EXTHDR_SIZE (3).
 */
int nx_frag_encode_exthdr(const nx_frag_header_t *fh,
                          uint8_t *buf, size_t buf_len);

/*
 * Decode a fragment extended header from a buffer.
 */
nx_err_t nx_frag_decode_exthdr(const uint8_t *buf, size_t buf_len,
                               nx_frag_header_t *fh);

#endif /* NEXUS_FRAGMENT_H */
