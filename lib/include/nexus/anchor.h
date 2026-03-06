/*
 * NEXUS Protocol -- ANCHOR Store-and-Forward Mailbox
 *
 * ANCHOR nodes store messages destined for offline nodes and deliver
 * them when the destination comes back online (detected via announcement).
 *
 * Each stored message is a complete packet (header + payload) kept
 * in a ring buffer. Messages expire after a configurable TTL.
 *
 * Memory budget (nRF52840):
 *   32 slots * 271 bytes (NX_MAX_PACKET) = ~8.5KB
 */
#ifndef NEXUS_ANCHOR_H
#define NEXUS_ANCHOR_H

#include "types.h"

/* ── Constants ───────────────────────────────────────────────────────── */
#define NX_ANCHOR_MAX_STORED    32
#define NX_ANCHOR_MAX_PER_DEST   8    /* Max messages per destination */
#define NX_ANCHOR_MSG_TTL_MS  3600000 /* 1 hour default */

/* ── Stored Message ──────────────────────────────────────────────────── */

typedef struct {
    nx_packet_t      pkt;
    nx_addr_short_t  dest;       /* Redundant with pkt.header.dst for fast lookup */
    uint64_t         stored_ms;  /* When this message was stored */
    bool             valid;
} nx_anchor_msg_t;

/* ── Mailbox ─────────────────────────────────────────────────────────── */

typedef struct {
    nx_anchor_msg_t  msgs[NX_ANCHOR_MAX_STORED];
    uint32_t         msg_ttl_ms;
} nx_anchor_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize the anchor mailbox. */
void nx_anchor_init(nx_anchor_t *a);

/* Set custom message TTL (default: NX_ANCHOR_MSG_TTL_MS). */
void nx_anchor_set_ttl(nx_anchor_t *a, uint32_t ttl_ms);

/* Expire old stored messages. */
void nx_anchor_expire(nx_anchor_t *a, uint64_t now_ms);

/*
 * Store a packet for later delivery.
 * Returns NX_ERR_FULL if mailbox or per-dest limit reached.
 */
nx_err_t nx_anchor_store(nx_anchor_t *a, const nx_packet_t *pkt,
                         uint64_t now_ms);

/*
 * Retrieve all stored messages for a destination.
 * Copies matching packets into out_pkts (up to max_pkts).
 * Returns the number of messages retrieved. Retrieved messages are
 * removed from the mailbox.
 */
int nx_anchor_retrieve(nx_anchor_t *a, const nx_addr_short_t *dest,
                       nx_packet_t *out_pkts, int max_pkts);

/* Count messages stored for a specific destination. */
int nx_anchor_count_for(const nx_anchor_t *a, const nx_addr_short_t *dest);

/* Count total stored messages. */
int nx_anchor_count(const nx_anchor_t *a);

#endif /* NEXUS_ANCHOR_H */
