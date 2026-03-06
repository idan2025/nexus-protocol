/*
 * NEXUS Protocol -- PRISM Routing Engine
 *
 * Proactive-Reactive Intelligent Segmented Mesh:
 * - Proactive: 1-hop neighbor table maintained via periodic beacons
 * - Reactive: On-demand RREQ/RREP route discovery for multi-hop paths
 * - Hierarchical: Domain segmentation (Phase 5, stubs here)
 *
 * Data structures are statically sized for MCU compatibility.
 */
#ifndef NEXUS_ROUTE_H
#define NEXUS_ROUTE_H

#include "types.h"

/* ── Table Sizes ─────────────────────────────────────────────────────── */
#define NX_MAX_NEIGHBORS     32
#define NX_MAX_ROUTES        64
#define NX_MAX_DEDUP         128
#define NX_MAX_PENDING_RREQ  16

/* ── Timeouts (ms) ───────────────────────────────────────────────────── */
#define NX_NEIGHBOR_TIMEOUT_MS   60000   /* 60s without beacon -> stale */
#define NX_ROUTE_TIMEOUT_MS     120000   /* 2 min route expiry */
#define NX_DEDUP_TIMEOUT_MS      30000   /* 30s dedup window */
#define NX_RREQ_TIMEOUT_MS       10000   /* 10s to wait for RREP */
#define NX_BEACON_INTERVAL_MS    15000   /* 15s beacon period */

/* ── Neighbor Entry ──────────────────────────────────────────────────── */
typedef struct {
    nx_addr_short_t addr;
    nx_addr_full_t  full_addr;
    uint8_t         sign_pubkey[NX_PUBKEY_SIZE];
    uint8_t         x25519_pubkey[NX_PUBKEY_SIZE];
    nx_role_t       role;
    int8_t          rssi;          /* Signal quality (transport-dependent) */
    uint8_t         link_quality;  /* 0-255, higher = better */
    uint64_t        last_seen_ms;
    bool            valid;
} nx_neighbor_t;

/* ── Route Entry (multi-hop path) ────────────────────────────────────── */
typedef struct {
    nx_addr_short_t dest;          /* Final destination */
    nx_addr_short_t next_hop;      /* Next hop toward dest */
    uint8_t         hop_count;     /* Total hops to dest */
    uint8_t         metric;        /* Route quality (lower = better) */
    uint8_t         via_transport; /* Transport index for this route (0-7) */
    uint64_t        expires_ms;    /* Monotonic timestamp */
    bool            valid;
} nx_route_t;

/* ── Dedup Entry ─────────────────────────────────────────────────────── */
typedef struct {
    nx_addr_short_t src;
    uint16_t        seq_id;
    uint64_t        expires_ms;
    bool            valid;
} nx_dedup_t;

/* ── Pending Route Request ───────────────────────────────────────────── */
typedef struct {
    nx_addr_short_t dest;
    uint16_t        rreq_id;
    uint64_t        expires_ms;
    bool            valid;
} nx_pending_rreq_t;

/* ── RREQ/RREP sub-types (carried in PTYPE_ROUTE payload) ───────────── */
typedef enum {
    NX_ROUTE_SUB_RREQ   = 0x01,
    NX_ROUTE_SUB_RREP   = 0x02,
    NX_ROUTE_SUB_RERR   = 0x03,
    NX_ROUTE_SUB_BEACON = 0x04,
} nx_route_subtype_t;

/*
 * RREQ payload: [subtype(1)][rreq_id(2)][origin(4)][dest(4)][hop_count(1)] = 12
 * RREP payload: [subtype(1)][rreq_id(2)][origin(4)][dest(4)][hop_count(1)][metric(1)] = 13
 * RERR payload: [subtype(1)][unreachable_dest(4)] = 5
 * BEACON payload: [subtype(1)][role(1)][neighbor_count(1)] = 3
 */
#define NX_RREQ_PAYLOAD_LEN   12
#define NX_RREP_PAYLOAD_LEN   13
#define NX_RERR_PAYLOAD_LEN    5
#define NX_BEACON_PAYLOAD_LEN  3

/* ── Routing Table ───────────────────────────────────────────────────── */
typedef struct {
    nx_neighbor_t      neighbors[NX_MAX_NEIGHBORS];
    nx_route_t         routes[NX_MAX_ROUTES];
    nx_dedup_t         dedup[NX_MAX_DEDUP];
    nx_pending_rreq_t  pending_rreq[NX_MAX_PENDING_RREQ];
    uint16_t           next_rreq_id;
    uint64_t           last_beacon_ms;
} nx_route_table_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize routing table. */
void nx_route_init(nx_route_table_t *rt);

/* Expire stale entries (call periodically). */
void nx_route_expire(nx_route_table_t *rt, uint64_t now_ms);

/* ── Neighbor Management ─────────────────────────────────────────────── */

/* Add or update a neighbor from a received announcement. */
nx_err_t nx_neighbor_update(nx_route_table_t *rt,
                            const nx_addr_short_t *addr,
                            const nx_addr_full_t *full_addr,
                            const uint8_t sign_pubkey[NX_PUBKEY_SIZE],
                            const uint8_t x25519_pubkey[NX_PUBKEY_SIZE],
                            nx_role_t role, int8_t rssi,
                            uint64_t now_ms);

/* Look up a neighbor by short address. Returns NULL if not found. */
const nx_neighbor_t *nx_neighbor_find(const nx_route_table_t *rt,
                                      const nx_addr_short_t *addr);

/* Count valid neighbors. */
int nx_neighbor_count(const nx_route_table_t *rt);

/* ── Route Management ────────────────────────────────────────────────── */

/* Add or update a route. Keeps the better metric if route exists. */
nx_err_t nx_route_update(nx_route_table_t *rt,
                         const nx_addr_short_t *dest,
                         const nx_addr_short_t *next_hop,
                         uint8_t hop_count, uint8_t metric,
                         uint64_t now_ms);

/* Look up next hop for a destination. Returns NULL if no route. */
const nx_route_t *nx_route_lookup(const nx_route_table_t *rt,
                                  const nx_addr_short_t *dest);

/* Remove routes that use a given next hop (link failure). */
int nx_route_invalidate_via(nx_route_table_t *rt,
                            const nx_addr_short_t *next_hop);

/* ── Dedup ───────────────────────────────────────────────────────────── */

/* Check if we've seen this (src, seq_id) recently. Returns true if dup. */
bool nx_dedup_check(nx_route_table_t *rt,
                    const nx_addr_short_t *src, uint16_t seq_id,
                    uint64_t now_ms);

/* ── RREQ/RREP ──────────────────────────────────────────────────────── */

/* Build RREQ payload into buf. Returns bytes written. */
int nx_route_build_rreq(nx_route_table_t *rt,
                        const nx_addr_short_t *origin,
                        const nx_addr_short_t *dest,
                        uint8_t *buf, size_t buf_len);

/* Build RREP payload into buf. Returns bytes written. */
int nx_route_build_rrep(uint16_t rreq_id,
                        const nx_addr_short_t *origin,
                        const nx_addr_short_t *dest,
                        uint8_t hop_count, uint8_t metric,
                        uint8_t *buf, size_t buf_len);

/* Build RERR payload into buf. Returns bytes written. */
int nx_route_build_rerr(const nx_addr_short_t *unreachable,
                        uint8_t *buf, size_t buf_len);

/* Build beacon payload into buf. Returns bytes written. */
int nx_route_build_beacon(nx_role_t role, const nx_route_table_t *rt,
                          uint8_t *buf, size_t buf_len);

/*
 * Process a received ROUTE packet payload.
 * Updates routing table as needed. Sets *out_subtype to the subtype processed.
 * For RREQ: installs reverse route, caller should forward or generate RREP.
 * For RREP: installs forward route.
 * For RERR: invalidates routes.
 */
nx_err_t nx_route_process(nx_route_table_t *rt,
                          const nx_addr_short_t *from_neighbor,
                          const uint8_t *payload, size_t len,
                          nx_route_subtype_t *out_subtype,
                          uint64_t now_ms);

#endif /* NEXUS_ROUTE_H */
