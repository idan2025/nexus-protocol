/*
 * NEXUS Protocol -- Group Encryption (Sender Keys)
 */
#include "nexus/group.h"
#include "nexus/crypto.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>

/* ── KDF: chain key ratchet (same pattern as session.c) ──────────────── */

static void kdf_ck(const uint8_t chain_key[32],
                   uint8_t next_chain[32], uint8_t msg_key[32])
{
    uint8_t tag1 = 0x01;
    uint8_t tag2 = 0x02;
    crypto_blake2b_keyed(next_chain, 32, chain_key, 32, &tag1, 1);
    crypto_blake2b_keyed(msg_key, 32, chain_key, 32, &tag2, 1);
}

/* Derive a member's chain key from group_key and their address. */
static void derive_chain_key(const uint8_t group_key[32],
                              const nx_addr_short_t *addr,
                              uint8_t chain_key[32])
{
    crypto_blake2b_keyed(chain_key, 32, group_key, 32,
                         addr->bytes, NX_SHORT_ADDR_SIZE);
}

/* Big-endian uint32 helpers. */
static void write_be32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static uint32_t read_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
}

/* ── Group Store ─────────────────────────────────────────────────────── */

void nx_group_store_init(nx_group_store_t *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

nx_group_t *nx_group_create(nx_group_store_t *store,
                             const nx_addr_short_t *group_id,
                             const uint8_t group_key[32],
                             const nx_addr_short_t *our_addr)
{
    if (!store || !group_id || !group_key || !our_addr) return NULL;

    /* Check if already exists */
    nx_group_t *existing = nx_group_find(store, group_id);
    if (existing) return existing;

    /* Find free slot */
    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (!store->groups[i].valid) {
            nx_group_t *g = &store->groups[i];
            memset(g, 0, sizeof(*g));
            g->group_id = *group_id;
            memcpy(g->group_key, group_key, 32);
            derive_chain_key(group_key, our_addr, g->send_chain_key);
            g->send_msg_num = 0;
            g->valid = true;
            return g;
        }
    }
    return NULL; /* Full */
}

nx_group_t *nx_group_find(nx_group_store_t *store,
                           const nx_addr_short_t *group_id)
{
    if (!store || !group_id) return NULL;

    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (store->groups[i].valid &&
            memcmp(store->groups[i].group_id.bytes, group_id->bytes,
                   NX_SHORT_ADDR_SIZE) == 0) {
            return &store->groups[i];
        }
    }
    return NULL;
}

void nx_group_remove(nx_group_store_t *store,
                      const nx_addr_short_t *group_id)
{
    if (!store || !group_id) return;

    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (store->groups[i].valid &&
            memcmp(store->groups[i].group_id.bytes, group_id->bytes,
                   NX_SHORT_ADDR_SIZE) == 0) {
            crypto_wipe(&store->groups[i], sizeof(nx_group_t));
            break;
        }
    }
}

nx_err_t nx_group_add_member(nx_group_t *g,
                              const nx_addr_short_t *member_addr,
                              const uint8_t group_key[32])
{
    if (!g || !member_addr || !group_key) return NX_ERR_INVALID_ARG;

    /* Check for existing member */
    for (int i = 0; i < NX_GROUP_MAX_MEMBERS; i++) {
        if (g->members[i].valid &&
            memcmp(g->members[i].addr.bytes, member_addr->bytes,
                   NX_SHORT_ADDR_SIZE) == 0) {
            return NX_ERR_ALREADY_EXISTS;
        }
    }

    /* Find free slot */
    for (int i = 0; i < NX_GROUP_MAX_MEMBERS; i++) {
        if (!g->members[i].valid) {
            g->members[i].addr = *member_addr;
            derive_chain_key(group_key, member_addr, g->members[i].chain_key);
            g->members[i].msg_num = 0;
            g->members[i].valid = true;
            return NX_OK;
        }
    }
    return NX_ERR_FULL;
}

int nx_group_count(const nx_group_store_t *store)
{
    if (!store) return 0;
    int c = 0;
    for (int i = 0; i < NX_GROUP_MAX; i++) {
        if (store->groups[i].valid) c++;
    }
    return c;
}

/* ── Encrypt ─────────────────────────────────────────────────────────── */

nx_err_t nx_group_encrypt(nx_group_t *g,
                           const uint8_t *pt, size_t pt_len,
                           uint8_t *out, size_t out_len,
                           size_t *out_written)
{
    if (!g || !out || !out_written) return NX_ERR_INVALID_ARG;
    if (pt_len > 0 && !pt) return NX_ERR_INVALID_ARG;
    if (pt_len > NX_GROUP_MAX_PLAINTEXT) return NX_ERR_BUFFER_TOO_SMALL;

    size_t needed = NX_GROUP_OVERHEAD + pt_len;
    if (out_len < needed) return NX_ERR_BUFFER_TOO_SMALL;

    /* Derive message key and advance send chain */
    uint8_t msg_key[32];
    uint8_t next_chain[32];
    kdf_ck(g->send_chain_key, next_chain, msg_key);
    memcpy(g->send_chain_key, next_chain, 32);
    crypto_wipe(next_chain, 32);

    /* Write header: [group_id(4)][msg_num(4)] */
    uint8_t *p = out;
    memcpy(p, g->group_id.bytes, NX_SHORT_ADDR_SIZE);  p += 4;
    write_be32(p, g->send_msg_num);                     p += 4;

    /* Generate nonce */
    uint8_t *nonce = p;                                 p += NX_NONCE_SIZE;
    nx_err_t err = nx_platform_random(nonce, NX_NONCE_SIZE);
    if (err != NX_OK) {
        crypto_wipe(msg_key, 32);
        return err;
    }

    /* Encrypt with AD = group_id(4) + msg_num(4) = 8 bytes */
    uint8_t *mac = p;                                   p += NX_MAC_SIZE;
    uint8_t *ct  = p;

    err = nx_crypto_aead_lock(msg_key, nonce, out, 8,
                              pt, pt_len, ct, mac);
    crypto_wipe(msg_key, 32);
    if (err != NX_OK) return err;

    g->send_msg_num++;
    *out_written = needed;
    return NX_OK;
}

/* ── Decrypt ─────────────────────────────────────────────────────────── */

nx_err_t nx_group_decrypt(nx_group_t *g,
                           const nx_addr_short_t *sender,
                           const uint8_t *in, size_t in_len,
                           uint8_t *pt, size_t pt_len,
                           size_t *pt_written)
{
    if (!g || !sender || !in || !pt || !pt_written)
        return NX_ERR_INVALID_ARG;
    if (in_len < NX_GROUP_OVERHEAD) return NX_ERR_BUFFER_TOO_SMALL;

    size_t ct_len = in_len - NX_GROUP_OVERHEAD;
    if (pt_len < ct_len) return NX_ERR_BUFFER_TOO_SMALL;

    /* Find sender in members table */
    nx_group_member_t *member = NULL;
    for (int i = 0; i < NX_GROUP_MAX_MEMBERS; i++) {
        if (g->members[i].valid &&
            memcmp(g->members[i].addr.bytes, sender->bytes,
                   NX_SHORT_ADDR_SIZE) == 0) {
            member = &g->members[i];
            break;
        }
    }
    if (!member) return NX_ERR_NOT_FOUND;

    /* Parse header */
    const uint8_t *p = in;
    /* group_id = p[0..3] */
    p += 4;
    uint32_t msg_num = read_be32(p);                    p += 4;
    const uint8_t *nonce = p;                           p += NX_NONCE_SIZE;
    const uint8_t *mac   = p;                           p += NX_MAC_SIZE;
    const uint8_t *ct    = p;

    /* Advance sender's chain to match msg_num (skip ahead if needed) */
    while (member->msg_num < msg_num) {
        uint8_t next_chain[32];
        uint8_t discard[32];
        kdf_ck(member->chain_key, next_chain, discard);
        memcpy(member->chain_key, next_chain, 32);
        crypto_wipe(next_chain, 32);
        crypto_wipe(discard, 32);
        member->msg_num++;
    }

    /* Derive this message's key */
    uint8_t msg_key[32];
    uint8_t next_chain[32];
    kdf_ck(member->chain_key, next_chain, msg_key);
    memcpy(member->chain_key, next_chain, 32);
    crypto_wipe(next_chain, 32);

    /* Decrypt with AD = first 8 bytes */
    nx_err_t err = nx_crypto_aead_unlock(msg_key, nonce, mac,
                                         in, 8, ct, ct_len, pt);
    crypto_wipe(msg_key, 32);
    if (err != NX_OK) return err;

    member->msg_num++;
    *pt_written = ct_len;
    return NX_OK;
}
