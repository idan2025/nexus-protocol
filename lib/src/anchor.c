/*
 * NEXUS Protocol -- ANCHOR Store-and-Forward Mailbox
 */
#include "nexus/anchor.h"
#include "nexus/identity.h"

#include <string.h>

void nx_anchor_init(nx_anchor_t *a)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->msg_ttl_ms = NX_ANCHOR_MSG_TTL_MS;
}

void nx_anchor_set_ttl(nx_anchor_t *a, uint32_t ttl_ms)
{
    if (a) a->msg_ttl_ms = ttl_ms;
}

void nx_anchor_expire(nx_anchor_t *a, uint64_t now_ms)
{
    if (!a) return;
    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
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

    /* Check per-dest limit */
    int dest_count = nx_anchor_count_for(a, &pkt->header.dst);
    if (dest_count >= NX_ANCHOR_MAX_PER_DEST) return NX_ERR_FULL;

    /* Find a free slot */
    int free_idx = -1;
    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
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
    for (int i = 0; i < NX_ANCHOR_MAX_STORED && count < max_pkts; i++) {
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
    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
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
    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
        if (a->msgs[i].valid) count++;
    }
    return count;
}
