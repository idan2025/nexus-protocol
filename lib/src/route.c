/*
 * NEXUS Protocol -- PRISM Routing Engine
 */
#include "nexus/route.h"
#include "nexus/identity.h"
#include "nexus/platform.h"

#include <string.h>

/* ── Big-endian helpers ──────────────────────────────────────────────── */

static void write_be16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

/* ── Init / Expire ───────────────────────────────────────────────────── */

void nx_route_init(nx_route_table_t *rt)
{
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
}

void nx_route_expire(nx_route_table_t *rt, uint64_t now_ms)
{
    if (!rt) return;

    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        if (rt->neighbors[i].valid &&
            now_ms - rt->neighbors[i].last_seen_ms > NX_NEIGHBOR_TIMEOUT_MS) {
            rt->neighbors[i].valid = false;
        }
    }
    for (int i = 0; i < NX_MAX_ROUTES; i++) {
        if (rt->routes[i].valid && now_ms > rt->routes[i].expires_ms) {
            rt->routes[i].valid = false;
        }
    }
    for (int i = 0; i < NX_MAX_DEDUP; i++) {
        if (rt->dedup[i].valid && now_ms > rt->dedup[i].expires_ms) {
            rt->dedup[i].valid = false;
        }
    }
    for (int i = 0; i < NX_MAX_PENDING_RREQ; i++) {
        if (rt->pending_rreq[i].valid &&
            now_ms > rt->pending_rreq[i].expires_ms) {
            rt->pending_rreq[i].valid = false;
        }
    }
}

/* ── Neighbor Management ─────────────────────────────────────────────── */

nx_err_t nx_neighbor_update(nx_route_table_t *rt,
                            const nx_addr_short_t *addr,
                            const nx_addr_full_t *full_addr,
                            const uint8_t sign_pubkey[NX_PUBKEY_SIZE],
                            const uint8_t x25519_pubkey[NX_PUBKEY_SIZE],
                            nx_role_t role, int8_t rssi,
                            uint64_t now_ms)
{
    if (!rt || !addr) return NX_ERR_INVALID_ARG;

    /* Try to find existing entry */
    int free_slot = -1;
    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        if (rt->neighbors[i].valid) {
            if (nx_addr_short_cmp(&rt->neighbors[i].addr, addr) == 0) {
                /* Update existing */
                nx_neighbor_t *n = &rt->neighbors[i];
                if (full_addr) n->full_addr = *full_addr;
                if (sign_pubkey)
                    memcpy(n->sign_pubkey, sign_pubkey, NX_PUBKEY_SIZE);
                if (x25519_pubkey)
                    memcpy(n->x25519_pubkey, x25519_pubkey, NX_PUBKEY_SIZE);
                n->role = role;
                n->rssi = rssi;
                n->last_seen_ms = now_ms;
                return NX_OK;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    /* Insert new */
    if (free_slot < 0) return NX_ERR_FULL;

    nx_neighbor_t *n = &rt->neighbors[free_slot];
    memset(n, 0, sizeof(*n));
    n->addr = *addr;
    if (full_addr) n->full_addr = *full_addr;
    if (sign_pubkey)
        memcpy(n->sign_pubkey, sign_pubkey, NX_PUBKEY_SIZE);
    if (x25519_pubkey)
        memcpy(n->x25519_pubkey, x25519_pubkey, NX_PUBKEY_SIZE);
    n->role = role;
    n->rssi = rssi;
    n->link_quality = 128; /* Default medium quality */
    n->last_seen_ms = now_ms;
    n->valid = true;
    return NX_OK;
}

const nx_neighbor_t *nx_neighbor_find(const nx_route_table_t *rt,
                                      const nx_addr_short_t *addr)
{
    if (!rt || !addr) return NULL;

    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        if (rt->neighbors[i].valid &&
            nx_addr_short_cmp(&rt->neighbors[i].addr, addr) == 0) {
            return &rt->neighbors[i];
        }
    }
    return NULL;
}

int nx_neighbor_count(const nx_route_table_t *rt)
{
    if (!rt) return 0;
    int count = 0;
    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        if (rt->neighbors[i].valid) count++;
    }
    return count;
}

/* ── Route Management ────────────────────────────────────────────────── */

nx_err_t nx_route_update(nx_route_table_t *rt,
                         const nx_addr_short_t *dest,
                         const nx_addr_short_t *next_hop,
                         uint8_t hop_count, uint8_t metric,
                         uint64_t now_ms)
{
    if (!rt || !dest || !next_hop) return NX_ERR_INVALID_ARG;

    /* Try to find existing route */
    int free_slot = -1;
    for (int i = 0; i < NX_MAX_ROUTES; i++) {
        if (rt->routes[i].valid) {
            if (nx_addr_short_cmp(&rt->routes[i].dest, dest) == 0) {
                /* Update if better or same */
                if (metric <= rt->routes[i].metric) {
                    rt->routes[i].next_hop   = *next_hop;
                    rt->routes[i].hop_count  = hop_count;
                    rt->routes[i].metric     = metric;
                    rt->routes[i].expires_ms = now_ms + NX_ROUTE_TIMEOUT_MS;
                }
                return NX_OK;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    /* Insert new */
    if (free_slot < 0) return NX_ERR_FULL;

    nx_route_t *r = &rt->routes[free_slot];
    r->dest       = *dest;
    r->next_hop   = *next_hop;
    r->hop_count  = hop_count;
    r->metric     = metric;
    r->expires_ms = now_ms + NX_ROUTE_TIMEOUT_MS;
    r->valid      = true;
    return NX_OK;
}

const nx_route_t *nx_route_lookup(const nx_route_table_t *rt,
                                  const nx_addr_short_t *dest)
{
    if (!rt || !dest) return NULL;

    for (int i = 0; i < NX_MAX_ROUTES; i++) {
        if (rt->routes[i].valid &&
            nx_addr_short_cmp(&rt->routes[i].dest, dest) == 0) {
            return &rt->routes[i];
        }
    }
    return NULL;
}

int nx_route_invalidate_via(nx_route_table_t *rt,
                            const nx_addr_short_t *next_hop)
{
    if (!rt || !next_hop) return 0;
    int removed = 0;
    for (int i = 0; i < NX_MAX_ROUTES; i++) {
        if (rt->routes[i].valid &&
            nx_addr_short_cmp(&rt->routes[i].next_hop, next_hop) == 0) {
            rt->routes[i].valid = false;
            removed++;
        }
    }
    return removed;
}

/* ── Dedup ───────────────────────────────────────────────────────────── */

bool nx_dedup_check(nx_route_table_t *rt,
                    const nx_addr_short_t *src, uint16_t seq_id,
                    uint64_t now_ms)
{
    if (!rt || !src) return false;

    int free_slot = -1;

    for (int i = 0; i < NX_MAX_DEDUP; i++) {
        if (rt->dedup[i].valid) {
            if (nx_addr_short_cmp(&rt->dedup[i].src, src) == 0 &&
                rt->dedup[i].seq_id == seq_id) {
                /* Already seen -- refresh expiry */
                rt->dedup[i].expires_ms = now_ms + NX_DEDUP_TIMEOUT_MS;
                return true;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    /* Not a dup -- record it */
    if (free_slot < 0) {
        /* Evict oldest */
        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < NX_MAX_DEDUP; i++) {
            if (rt->dedup[i].expires_ms < oldest) {
                oldest = oldest;
                free_slot = i;
            }
        }
        if (free_slot < 0) free_slot = 0; /* Shouldn't happen */
    }

    rt->dedup[free_slot].src        = *src;
    rt->dedup[free_slot].seq_id     = seq_id;
    rt->dedup[free_slot].expires_ms = now_ms + NX_DEDUP_TIMEOUT_MS;
    rt->dedup[free_slot].valid      = true;
    return false;
}

/* ── RREQ / RREP / RERR / BEACON builders ───────────────────────────── */

int nx_route_build_rreq(nx_route_table_t *rt,
                        const nx_addr_short_t *origin,
                        const nx_addr_short_t *dest,
                        uint8_t *buf, size_t buf_len)
{
    if (!rt || !origin || !dest || !buf) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_RREQ_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    uint16_t rreq_id = rt->next_rreq_id++;

    /* Record pending RREQ */
    for (int i = 0; i < NX_MAX_PENDING_RREQ; i++) {
        if (!rt->pending_rreq[i].valid) {
            rt->pending_rreq[i].dest       = *dest;
            rt->pending_rreq[i].rreq_id    = rreq_id;
            rt->pending_rreq[i].expires_ms =
                nx_platform_time_ms() + NX_RREQ_TIMEOUT_MS;
            rt->pending_rreq[i].valid      = true;
            break;
        }
    }

    buf[0] = (uint8_t)NX_ROUTE_SUB_RREQ;
    write_be16(&buf[1], rreq_id);
    memcpy(&buf[3], origin->bytes, NX_SHORT_ADDR_SIZE);
    memcpy(&buf[7], dest->bytes, NX_SHORT_ADDR_SIZE);
    buf[11] = 0; /* hop_count starts at 0 */

    return NX_RREQ_PAYLOAD_LEN;
}

int nx_route_build_rrep(uint16_t rreq_id,
                        const nx_addr_short_t *origin,
                        const nx_addr_short_t *dest,
                        uint8_t hop_count, uint8_t metric,
                        uint8_t *buf, size_t buf_len)
{
    if (!origin || !dest || !buf) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_RREP_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = (uint8_t)NX_ROUTE_SUB_RREP;
    write_be16(&buf[1], rreq_id);
    memcpy(&buf[3], origin->bytes, NX_SHORT_ADDR_SIZE);
    memcpy(&buf[7], dest->bytes, NX_SHORT_ADDR_SIZE);
    buf[11] = hop_count;
    buf[12] = metric;

    return NX_RREP_PAYLOAD_LEN;
}

int nx_route_build_rerr(const nx_addr_short_t *unreachable,
                        uint8_t *buf, size_t buf_len)
{
    if (!unreachable || !buf) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_RERR_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = (uint8_t)NX_ROUTE_SUB_RERR;
    memcpy(&buf[1], unreachable->bytes, NX_SHORT_ADDR_SIZE);

    return NX_RERR_PAYLOAD_LEN;
}

int nx_route_build_beacon(nx_role_t role, const nx_route_table_t *rt,
                          uint8_t *buf, size_t buf_len)
{
    if (!rt || !buf) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_BEACON_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = (uint8_t)NX_ROUTE_SUB_BEACON;
    buf[1] = (uint8_t)role;
    buf[2] = (uint8_t)nx_neighbor_count(rt);

    return NX_BEACON_PAYLOAD_LEN;
}

/* ── Process incoming ROUTE packets ──────────────────────────────────── */

nx_err_t nx_route_process(nx_route_table_t *rt,
                          const nx_addr_short_t *from_neighbor,
                          const uint8_t *payload, size_t len,
                          nx_route_subtype_t *out_subtype,
                          uint64_t now_ms)
{
    if (!rt || !from_neighbor || !payload || len < 1 || !out_subtype)
        return NX_ERR_INVALID_ARG;

    *out_subtype = (nx_route_subtype_t)payload[0];

    switch (*out_subtype) {
    case NX_ROUTE_SUB_RREQ: {
        if (len < NX_RREQ_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;
        /* uint16_t rreq_id = read_be16(&payload[1]); */
        nx_addr_short_t origin;
        memcpy(origin.bytes, &payload[3], NX_SHORT_ADDR_SIZE);
        uint8_t hop_count = payload[11];

        /* Install reverse route to origin via the neighbor who sent this */
        nx_route_update(rt, &origin, from_neighbor,
                        hop_count + 1, hop_count + 1, now_ms);
        return NX_OK;
    }
    case NX_ROUTE_SUB_RREP: {
        if (len < NX_RREP_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;
        nx_addr_short_t dest;
        memcpy(dest.bytes, &payload[7], NX_SHORT_ADDR_SIZE);
        uint8_t hop_count = payload[11];
        uint8_t metric    = payload[12];

        /* Install forward route to dest via the neighbor who sent this */
        nx_route_update(rt, &dest, from_neighbor,
                        hop_count, metric, now_ms);
        return NX_OK;
    }
    case NX_ROUTE_SUB_RERR: {
        if (len < NX_RERR_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;
        nx_addr_short_t unreachable;
        memcpy(unreachable.bytes, &payload[1], NX_SHORT_ADDR_SIZE);
        nx_route_invalidate_via(rt, &unreachable);
        return NX_OK;
    }
    case NX_ROUTE_SUB_BEACON:
        /* Beacon processing is handled at the node level (neighbor update) */
        return NX_OK;
    default:
        return NX_ERR_INVALID_ARG;
    }
}
