/*
 * NEXUS Protocol -- Group Encryption (Sender Keys)
 *
 * Sender-key based group messaging with chain ratchet.
 * Each member derives a unique sender chain from the shared group key
 * and their own address. Messages are broadcast-encrypted so all
 * group members can decrypt.
 *
 * Wire format: [group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]
 * AD = group_id(4) + msg_num(4) = 8 bytes
 */
#ifndef NEXUS_GROUP_H
#define NEXUS_GROUP_H

#include "types.h"

/* ── Limits ──────────────────────────────────────────────────────────── */
#define NX_GROUP_MAX           8
#define NX_GROUP_MAX_MEMBERS  16
#define NX_GROUP_OVERHEAD     48   /* group_id(4) + msg_num(4) + nonce(24) + MAC(16) */
#define NX_GROUP_MAX_PLAINTEXT (NX_MAX_PAYLOAD - 1 - NX_GROUP_OVERHEAD)  /* 193 */

/* ── Extended Header Type ────────────────────────────────────────────── */
#define NX_EXTHDR_GROUP_MSG   0x20

/* ── Group Member ────────────────────────────────────────────────────── */

typedef struct {
    nx_addr_short_t addr;
    uint8_t         chain_key[NX_SYMMETRIC_KEY_SIZE];
    uint32_t        msg_num;        /* Next expected message number */
    bool            valid;
} nx_group_member_t;

/* ── Group ───────────────────────────────────────────────────────────── */

typedef struct {
    nx_addr_short_t    group_id;
    uint8_t            group_key[NX_SYMMETRIC_KEY_SIZE];
    uint8_t            send_chain_key[NX_SYMMETRIC_KEY_SIZE];
    uint32_t           send_msg_num;
    nx_group_member_t  members[NX_GROUP_MAX_MEMBERS];
    bool               valid;
} nx_group_t;

/* ── Group Store ─────────────────────────────────────────────────────── */

typedef struct {
    nx_group_t groups[NX_GROUP_MAX];
} nx_group_store_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize a group store. */
void nx_group_store_init(nx_group_store_t *store);

/* Create a new group. Derives our send chain from group_key + our_addr. */
nx_group_t *nx_group_create(nx_group_store_t *store,
                             const nx_addr_short_t *group_id,
                             const uint8_t group_key[NX_SYMMETRIC_KEY_SIZE],
                             const nx_addr_short_t *our_addr);

/* Find a group by ID. Returns NULL if not found. */
nx_group_t *nx_group_find(nx_group_store_t *store,
                           const nx_addr_short_t *group_id);

/* Remove a group, wiping all keys. */
void nx_group_remove(nx_group_store_t *store,
                      const nx_addr_short_t *group_id);

/* Add a member to a group. Derives their chain key from group_key + member_addr. */
nx_err_t nx_group_add_member(nx_group_t *g,
                              const nx_addr_short_t *member_addr,
                              const uint8_t group_key[NX_SYMMETRIC_KEY_SIZE]);

/* Count valid groups. */
int nx_group_count(const nx_group_store_t *store);

/*
 * Encrypt a group message.
 * out layout: [group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]
 */
nx_err_t nx_group_encrypt(nx_group_t *g,
                           const uint8_t *pt, size_t pt_len,
                           uint8_t *out, size_t out_len,
                           size_t *out_written);

/*
 * Decrypt a group message from a known sender.
 * in layout: [group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]
 */
nx_err_t nx_group_decrypt(nx_group_t *g,
                           const nx_addr_short_t *sender,
                           const uint8_t *in, size_t in_len,
                           uint8_t *pt, size_t pt_len,
                           size_t *pt_written);

#endif /* NEXUS_GROUP_H */
