/*
 * NEXUS Protocol -- Linked Sessions (Double Ratchet)
 *
 * Provides per-message forward secrecy via a Signal-style Double Ratchet.
 *
 * ── Handshake (simplified X3DH) ──
 * Alice wants to talk to Bob. She knows Bob's identity X25519 pubkey
 * (from his announcement).
 *
 * 1. Alice generates ephemeral X25519 keypair (EKa).
 * 2. Alice computes: DH1 = X25519(IKa_secret, IKb_pub)   [identity-identity]
 *                    DH2 = X25519(EKa_secret, IKb_pub)   [ephemeral-identity]
 *    root_key = BLAKE2b-256(DH1 || DH2)
 * 3. Alice sends SESSION_INIT: [EKa_pub(32)]
 * 4. Bob receives, computes same DH1/DH2 -> root_key.
 * 5. Bob generates his own ephemeral EKb, sends SESSION_ACK: [EKb_pub(32)]
 * 6. Both do first DH ratchet step with EKa/EKb.
 *
 * ── Double Ratchet ──
 * Each message uses a unique message key derived from a chain key.
 * DH ratchet advances when the sender changes (Alice->Bob vs Bob->Alice).
 *
 * Chain KDF: BLAKE2b-keyed(chain_key, 0x01) -> next_chain_key
 *            BLAKE2b-keyed(chain_key, 0x02) -> message_key
 * DH ratchet: root_key, DH_out = KDF_RK(root_key, DH(our_eph, their_eph))
 *
 * ── Wire format ──
 * Session payload: [msg_num(4)][prev_chain_len(4)][DH_pub(32)][nonce(24)][MAC(16)][ciphertext]
 * Overhead: 80 bytes
 */
#ifndef NEXUS_SESSION_H
#define NEXUS_SESSION_H

#include "types.h"

/* ── Constants ───────────────────────────────────────────────────────── */
#ifndef NX_SESSION_MAX
#define NX_SESSION_MAX           16    /* Max concurrent sessions */
#endif
#define NX_SESSION_OVERHEAD      80    /* 4+4+32+24+16 */
#ifndef NX_SESSION_MAX_SKIP
#define NX_SESSION_MAX_SKIP      32    /* Max skipped message keys to store */
#endif
#define NX_SESSION_INIT_LEN      32    /* Handshake init: ephemeral pubkey */
#define NX_SESSION_ACK_LEN       32    /* Handshake ack: ephemeral pubkey */

/* Session packet sub-types (carried in DATA payload with EXTHDR) */
typedef enum {
    NX_SESSION_SUB_INIT     = 0x10,  /* Handshake initiation */
    NX_SESSION_SUB_ACK      = 0x11,  /* Handshake acknowledgment */
    NX_SESSION_SUB_MSG      = 0x12,  /* Ratcheted message */
} nx_session_subtype_t;

/* ── Chain State ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  key[NX_SYMMETRIC_KEY_SIZE];  /* Chain key (32B) */
    uint32_t n;                            /* Message number */
} nx_chain_t;

/* ── Skipped Message Key ─────────────────────────────────────────────── */

typedef struct {
    uint8_t  dh_pub[NX_PUBKEY_SIZE];       /* DH ratchet pubkey at time of skip */
    uint32_t n;                             /* Message number */
    uint8_t  msg_key[NX_SYMMETRIC_KEY_SIZE];
    bool     valid;
} nx_skipped_key_t;

/* ── Session State ───────────────────────────────────────────────────── */

typedef struct {
    /* Peer identity */
    nx_addr_short_t   peer_addr;
    uint8_t           peer_x25519_pub[NX_PUBKEY_SIZE]; /* Long-term identity key */

    /* DH ratchet */
    uint8_t           dh_secret[NX_PRIVKEY_SIZE];  /* Our current ephemeral secret */
    uint8_t           dh_public[NX_PUBKEY_SIZE];   /* Our current ephemeral public */
    uint8_t           dh_remote[NX_PUBKEY_SIZE];   /* Their current ephemeral public */

    /* Root and chain keys */
    uint8_t           root_key[NX_SYMMETRIC_KEY_SIZE];
    nx_chain_t        send_chain;
    nx_chain_t        recv_chain;
    uint32_t          prev_send_n;  /* Previous sending chain length */

    /* Skipped message keys (for out-of-order) */
    nx_skipped_key_t  skipped[NX_SESSION_MAX_SKIP];

    /* State */
    bool              established;  /* Handshake complete */
    bool              valid;
} nx_session_t;

/* ── Session Store ───────────────────────────────────────────────────── */

typedef struct {
    nx_session_t sessions[NX_SESSION_MAX];
} nx_session_store_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/* Initialize the session store. */
void nx_session_store_init(nx_session_store_t *store);

/* Find a session by peer address. Returns NULL if none. */
nx_session_t *nx_session_find(nx_session_store_t *store,
                              const nx_addr_short_t *peer);

/* Allocate a new session slot for a peer. Returns NULL if full. */
nx_session_t *nx_session_alloc(nx_session_store_t *store,
                               const nx_addr_short_t *peer);

/* Remove a session. Wipes keys. */
void nx_session_remove(nx_session_store_t *store,
                       const nx_addr_short_t *peer);

/* Count active sessions. */
int nx_session_count(const nx_session_store_t *store);

/* ── Handshake ───────────────────────────────────────────────────────── */

/*
 * Initiate a session (Alice side).
 * Computes DH, derives root key, generates ephemeral DH keypair.
 * Writes SESSION_INIT payload (32 bytes: ephemeral pubkey) into out_payload.
 */
nx_err_t nx_session_initiate(nx_session_t *s,
                             const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
                             const uint8_t our_x25519_public[NX_PUBKEY_SIZE],
                             const uint8_t peer_x25519_pub[NX_PUBKEY_SIZE],
                             uint8_t *out_payload, size_t buf_len);

/*
 * Accept a session (Bob side).
 * Receives Alice's ephemeral pubkey, computes DH, derives root key.
 * Writes SESSION_ACK payload (32 bytes: our ephemeral pubkey) into out_payload.
 */
nx_err_t nx_session_accept(nx_session_t *s,
                           const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
                           const uint8_t our_x25519_public[NX_PUBKEY_SIZE],
                           const uint8_t peer_x25519_pub[NX_PUBKEY_SIZE],
                           const uint8_t *init_payload, size_t init_len,
                           uint8_t *out_payload, size_t buf_len);

/*
 * Complete session initiation (Alice receives ACK).
 * Performs the first DH ratchet step.
 */
nx_err_t nx_session_complete(nx_session_t *s,
                             const uint8_t *ack_payload, size_t ack_len);

/* ── Encrypt / Decrypt ───────────────────────────────────────────────── */

/*
 * Encrypt a message using the session ratchet.
 * out_buf layout: [msg_num(4)][prev_n(4)][DH_pub(32)][nonce(24)][MAC(16)][ct]
 * Advances the sending chain.
 */
nx_err_t nx_session_encrypt(nx_session_t *s,
                            const uint8_t *plaintext, size_t plaintext_len,
                            uint8_t *out_buf, size_t out_buf_len,
                            size_t *out_len);

/*
 * Decrypt a message using the session ratchet.
 * Handles DH ratchet advancement and out-of-order messages.
 */
nx_err_t nx_session_decrypt(nx_session_t *s,
                            const uint8_t *in_buf, size_t in_len,
                            uint8_t *plaintext, size_t plaintext_buf_len,
                            size_t *plaintext_len);

#endif /* NEXUS_SESSION_H */
