/*
 * NEXUS Protocol -- Store-and-Forward Mailbox
 *
 * All RELAY+ nodes store messages destined for offline nodes and deliver
 * them when the destination comes back online (detected via announcement).
 *
 * Storage tiers:
 *   RELAY/GATEWAY:  NX_ANCHOR_RELAY_STORED (8 slots, 30min TTL)
 *   ANCHOR:         NX_ANCHOR_MAX_STORED (32 slots, 1hr TTL)
 *   VAULT:          NX_ANCHOR_VAULT_STORED (256 slots, 24hr TTL)
 *
 * Memory budget:
 *   RELAY:  8 * 271B  =  ~2.1KB (fits any MCU)
 *   ANCHOR: 32 * 271B =  ~8.5KB (fits nRF52840)
 *   VAULT:  256 * 271B = ~68KB  (Linux/Android only)
 */
#ifndef NEXUS_ANCHOR_H
#define NEXUS_ANCHOR_H

#include "types.h"

/* ── Constants ───────────────────────────────────────────────────────── */

/* Tier sizes -- the actual anchor struct uses MAX for static allocation.
 * Nodes use a "live slots" limit based on their role. */
#define NX_ANCHOR_RELAY_STORED   8
#define NX_ANCHOR_MAX_STORED    32
#define NX_ANCHOR_VAULT_STORED 256
#define NX_ANCHOR_MAX_PER_DEST   8    /* Max messages per destination */

/* TTL defaults per tier */
#define NX_ANCHOR_RELAY_TTL_MS   1800000  /* 30 minutes */
#define NX_ANCHOR_MSG_TTL_MS     3600000  /* 1 hour (ANCHOR default) */
#define NX_ANCHOR_VAULT_TTL_MS  86400000  /* 24 hours */

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
    int              max_slots;   /* Effective limit based on role tier */
} nx_anchor_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize the anchor mailbox with default (ANCHOR-tier) settings. */
void nx_anchor_init(nx_anchor_t *a);

/* Configure anchor for a specific role tier.
 * Call after nx_anchor_init(). Sets max_slots and TTL. */
void nx_anchor_configure_for_role(nx_anchor_t *a, nx_role_t role);

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
