/*
 * NEXUS Protocol -- Store-and-Forward Mailbox
 */
#include "nexus/anchor.h"
#include "nexus/identity.h"
#include "nexus/packet.h"
#include "monocypher/monocypher.h"

#include <stddef.h>
#include <string.h>

void nx_anchor_init(nx_anchor_t *a)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->msg_ttl_ms = NX_ANCHOR_MSG_TTL_MS;
    a->max_slots  = NX_ANCHOR_MAX_STORED;
}

void nx_anchor_configure_for_role(nx_anchor_t *a, nx_role_t role)
{
    if (!a) return;

    switch (role) {
    case NX_ROLE_VAULT:
        /* VAULT: large buffer, long TTL.
         * Clamped to NX_ANCHOR_MAX_STORED since that's the static array size.
         * On Linux/Android, rebuild with larger NX_ANCHOR_MAX_STORED for true
         * vault capacity. */
        a->max_slots  = NX_ANCHOR_MAX_STORED; /* Use full static array */
        a->msg_ttl_ms = NX_ANCHOR_VAULT_TTL_MS;
        break;
    case NX_ROLE_PILLAR:
    case NX_ROLE_SENTINEL:
    case NX_ROLE_ANCHOR:
        a->max_slots  = NX_ANCHOR_MAX_STORED;
        a->msg_ttl_ms = NX_ANCHOR_MSG_TTL_MS;
        break;
    case NX_ROLE_RELAY:
    case NX_ROLE_GATEWAY:
        a->max_slots  = NX_ANCHOR_RELAY_STORED;
        a->msg_ttl_ms = NX_ANCHOR_RELAY_TTL_MS;
        break;
    default:
        /* LEAF: no storage */
        a->max_slots  = 0;
        a->msg_ttl_ms = 0;
        break;
    }
}

void nx_anchor_set_ttl(nx_anchor_t *a, uint32_t ttl_ms)
{
    if (a) a->msg_ttl_ms = ttl_ms;
}

void nx_anchor_expire(nx_anchor_t *a, uint64_t now_ms)
{
    if (!a) return;
    for (int i = 0; i < a->max_slots && i < NX_ANCHOR_MAX_STORED; i++) {
        if (a->msgs[i].valid &&
            now_ms - a->msgs[i].stored_ms > a->msg_ttl_ms) {
            a->msgs[i].valid = false;
        }
    }
}

nx_err_t nx_anchor_store(nx_anchor_t *a, const nx_packet_t *pkt,
                         uint64_t now_ms)
{
    if (!a || !pkt) return NX_ERR_INVALID_ARG;
    if (a->max_slots <= 0) return NX_ERR_FULL;

    /* Check per-dest limit */
    int dest_count = nx_anchor_count_for(a, &pkt->header.dst);
    if (dest_count >= NX_ANCHOR_MAX_PER_DEST) return NX_ERR_FULL;

    /* Find a free slot within our tier limit */
    int free_idx = -1;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    for (int i = 0; i < limit; i++) {
        if (!a->msgs[i].valid) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) return NX_ERR_FULL;

    a->msgs[free_idx].pkt       = *pkt;
    a->msgs[free_idx].dest      = pkt->header.dst;
    a->msgs[free_idx].stored_ms = now_ms;
    a->msgs[free_idx].valid     = true;

    return NX_OK;
}

int nx_anchor_retrieve(nx_anchor_t *a, const nx_addr_short_t *dest,
                       nx_packet_t *out_pkts, int max_pkts)
{
    if (!a || !dest || !out_pkts || max_pkts <= 0) return 0;

    int count = 0;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    for (int i = 0; i < limit && count < max_pkts; i++) {
        if (a->msgs[i].valid &&
            nx_addr_short_cmp(&a->msgs[i].dest, dest) == 0) {
            out_pkts[count++] = a->msgs[i].pkt;
            a->msgs[i].valid = false;
        }
    }
    return count;
}

int nx_anchor_count_for(const nx_anchor_t *a, const nx_addr_short_t *dest)
{
    if (!a || !dest) return 0;
    int count = 0;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    for (int i = 0; i < limit; i++) {
        if (a->msgs[i].valid &&
            nx_addr_short_cmp(&a->msgs[i].dest, dest) == 0) {
            count++;
        }
    }
    return count;
}

int nx_anchor_count(const nx_anchor_t *a)
{
    if (!a) return 0;
    int count = 0;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    for (int i = 0; i < limit; i++) {
        if (a->msgs[i].valid) count++;
    }
    return count;
}

void nx_anchor_msg_id(const nx_packet_t *pkt,
                      uint8_t out[NX_ANCHOR_MSG_ID_SIZE])
{
    if (!out) return;
    if (!pkt) { memset(out, 0, NX_ANCHOR_MSG_ID_SIZE); return; }

    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(pkt, wire, sizeof(wire));
    if (n <= 0) {
        memset(out, 0, NX_ANCHOR_MSG_ID_SIZE);
        return;
    }
    uint8_t full[32];
    crypto_blake2b(full, sizeof(full), wire, (size_t)n);
    memcpy(out, full, NX_ANCHOR_MSG_ID_SIZE);
}

int nx_anchor_list_ids(const nx_anchor_t *a,
                       uint8_t (*ids)[NX_ANCHOR_MSG_ID_SIZE], int max)
{
    if (!a || max <= 0 || !ids) return 0;
    int out = 0;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    for (int i = 0; i < limit && out < max; i++) {
        if (a->msgs[i].valid) {
            nx_anchor_msg_id(&a->msgs[i].pkt, ids[out]);
            out++;
        }
    }
    return out;
}

const nx_packet_t *nx_anchor_find_by_id(const nx_anchor_t *a,
                                        const uint8_t id[NX_ANCHOR_MSG_ID_SIZE])
{
    if (!a || !id) return NULL;
    int limit = a->max_slots < NX_ANCHOR_MAX_STORED ?
                a->max_slots : NX_ANCHOR_MAX_STORED;
    uint8_t slot_id[NX_ANCHOR_MSG_ID_SIZE];
    for (int i = 0; i < limit; i++) {
        if (!a->msgs[i].valid) continue;
        nx_anchor_msg_id(&a->msgs[i].pkt, slot_id);
        if (memcmp(slot_id, id, NX_ANCHOR_MSG_ID_SIZE) == 0) {
            return &a->msgs[i].pkt;
        }
    }
    return NULL;
}

bool nx_anchor_has_id(const nx_anchor_t *a,
                      const uint8_t id[NX_ANCHOR_MSG_ID_SIZE])
{
    return nx_anchor_find_by_id(a, id) != NULL;
}
