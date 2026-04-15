/*
 * NEXUS Protocol -- Linked Sessions (Double Ratchet)
 */
#include "nexus/session.h"
#include "nexus/crypto.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>

/* ── KDF helpers ─────────────────────────────────────────────────────── */

/* KDF_RK: root key ratchet. Derives new root key + chain key from DH output. */
static void kdf_rk(const uint8_t root_key[32], const uint8_t dh_out[32],
                   uint8_t new_root[32], uint8_t new_chain[32])
{
    /* Concatenate root_key || dh_out as input, use root_key as BLAKE2b key */
    uint8_t input[64];
    memcpy(input, root_key, 32);
    memcpy(input + 32, dh_out, 32);

    /* Derive 64 bytes: first 32 = new root, second 32 = new chain */
    uint8_t derived[64];
    crypto_blake2b_keyed(derived, 64, root_key, 32, input, 64);

    memcpy(new_root, derived, 32);
    memcpy(new_chain, derived + 32, 32);
    crypto_wipe(derived, sizeof(derived));
    crypto_wipe(input, sizeof(input));
}

/* KDF_CK: chain key ratchet. Derives message key and next chain key. */
static void kdf_ck(const uint8_t chain_key[32],
                   uint8_t next_chain[32], uint8_t msg_key[32])
{
    uint8_t tag1 = 0x01;
    uint8_t tag2 = 0x02;

    crypto_blake2b_keyed(next_chain, 32, chain_key, 32, &tag1, 1);
    crypto_blake2b_keyed(msg_key, 32, chain_key, 32, &tag2, 1);
}

/* Generate fresh DH keypair. */
static nx_err_t generate_dh(uint8_t secret[32], uint8_t public_key[32])
{
    nx_err_t err = nx_platform_random(secret, 32);
    if (err != NX_OK) return err;
    crypto_x25519_public_key(public_key, secret);
    return NX_OK;
}

/* X25519 DH then BLAKE2b to get uniform output. */
static void dh(const uint8_t secret[32], const uint8_t their_pub[32],
               uint8_t out[32])
{
    uint8_t raw[32];
    crypto_x25519(raw, secret, their_pub);
    crypto_blake2b(out, 32, raw, 32);
    crypto_wipe(raw, sizeof(raw));
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

/* ── Session Store ───────────────────────────────────────────────────── */

void nx_session_store_init(nx_session_store_t *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

nx_session_t *nx_session_find(nx_session_store_t *store,
                              const nx_addr_short_t *peer)
{
    if (!store || !peer) return NULL;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (store->sessions[i].valid &&
            memcmp(store->sessions[i].peer_addr.bytes, peer->bytes, NX_SHORT_ADDR_SIZE) == 0) {
            return &store->sessions[i];
        }
    }
    return NULL;
}

nx_session_t *nx_session_alloc(nx_session_store_t *store,
                               const nx_addr_short_t *peer)
{
    if (!store || !peer) return NULL;

    /* Check if already exists */
    nx_session_t *existing = nx_session_find(store, peer);
    if (existing) return existing;

    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (!store->sessions[i].valid) {
            memset(&store->sessions[i], 0, sizeof(nx_session_t));
            store->sessions[i].peer_addr = *peer;
            store->sessions[i].valid = true;
            return &store->sessions[i];
        }
    }
    return NULL;
}

void nx_session_remove(nx_session_store_t *store,
                       const nx_addr_short_t *peer)
{
    if (!store || !peer) return;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (store->sessions[i].valid &&
            memcmp(store->sessions[i].peer_addr.bytes, peer->bytes, NX_SHORT_ADDR_SIZE) == 0) {
            crypto_wipe(&store->sessions[i], sizeof(nx_session_t));
            break;
        }
    }
}

int nx_session_count(const nx_session_store_t *store)
{
    if (!store) return 0;
    int c = 0;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (store->sessions[i].valid) c++;
    }
    return c;
}

/* ── Handshake ───────────────────────────────────────────────────────── */

nx_err_t nx_session_initiate(nx_session_t *s,
                             const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
                             const uint8_t our_x25519_public[NX_PUBKEY_SIZE],
                             const uint8_t peer_x25519_pub[NX_PUBKEY_SIZE],
                             uint8_t *out_payload, size_t buf_len)
{
    if (!s || !our_x25519_secret || !our_x25519_public || !peer_x25519_pub ||
        !out_payload)
        return NX_ERR_INVALID_ARG;
    if (buf_len < NX_SESSION_INIT_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(s->peer_x25519_pub, peer_x25519_pub, NX_PUBKEY_SIZE);

    /* Generate ephemeral DH keypair for ratchet */
    nx_err_t err = generate_dh(s->dh_secret, s->dh_public);
    if (err != NX_OK) return err;

    /* DH1: identity-identity */
    uint8_t dh1[32];
    dh(our_x25519_secret, peer_x25519_pub, dh1);

    /* DH2: ephemeral-identity */
    uint8_t dh2[32];
    dh(s->dh_secret, peer_x25519_pub, dh2);

    /* Root key = BLAKE2b(DH1 || DH2) */
    uint8_t combined[64];
    memcpy(combined, dh1, 32);
    memcpy(combined + 32, dh2, 32);
    crypto_blake2b(s->root_key, 32, combined, 64);
    crypto_wipe(dh1, 32);
    crypto_wipe(dh2, 32);
    crypto_wipe(combined, 64);

    /* Output: our ephemeral pubkey */
    memcpy(out_payload, s->dh_public, NX_PUBKEY_SIZE);

    /* Session not fully established until we receive ACK */
    s->established = false;
    return NX_OK;
}

nx_err_t nx_session_accept(nx_session_t *s,
                           const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
                           const uint8_t our_x25519_public[NX_PUBKEY_SIZE],
                           const uint8_t peer_x25519_pub[NX_PUBKEY_SIZE],
                           const uint8_t *init_payload, size_t init_len,
                           uint8_t *out_payload, size_t buf_len)
{
    if (!s || !our_x25519_secret || !our_x25519_public || !peer_x25519_pub ||
        !init_payload || !out_payload)
        return NX_ERR_INVALID_ARG;
    if (init_len < NX_SESSION_INIT_LEN) return NX_ERR_BUFFER_TOO_SMALL;
    if (buf_len < NX_SESSION_ACK_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(s->peer_x25519_pub, peer_x25519_pub, NX_PUBKEY_SIZE);

    /* Extract Alice's ephemeral pubkey */
    memcpy(s->dh_remote, init_payload, NX_PUBKEY_SIZE);

    /* DH1: identity-identity (reversed) */
    uint8_t dh1[32];
    dh(our_x25519_secret, peer_x25519_pub, dh1);

    /* DH2: identity-ephemeral (reversed) */
    uint8_t dh2[32];
    dh(our_x25519_secret, s->dh_remote, dh2);

    /* Root key = BLAKE2b(DH1 || DH2) */
    uint8_t combined[64];
    memcpy(combined, dh1, 32);
    memcpy(combined + 32, dh2, 32);
    crypto_blake2b(s->root_key, 32, combined, 64);
    crypto_wipe(dh1, 32);
    crypto_wipe(dh2, 32);
    crypto_wipe(combined, 64);

    /* Generate our ephemeral DH keypair */
    nx_err_t err = generate_dh(s->dh_secret, s->dh_public);
    if (err != NX_OK) return err;

    /* Perform first DH ratchet: DH(our_eph, their_eph) */
    uint8_t dh_out[32];
    dh(s->dh_secret, s->dh_remote, dh_out);

    uint8_t new_root[32];
    kdf_rk(s->root_key, dh_out, new_root, s->send_chain.key);
    memcpy(s->root_key, new_root, 32);
    s->send_chain.n = 0;
    crypto_wipe(dh_out, 32);
    crypto_wipe(new_root, 32);

    /* Output: our ephemeral pubkey */
    memcpy(out_payload, s->dh_public, NX_PUBKEY_SIZE);

    s->established = true;
    return NX_OK;
}

nx_err_t nx_session_complete(nx_session_t *s,
                             const uint8_t *ack_payload, size_t ack_len)
{
    if (!s || !ack_payload) return NX_ERR_INVALID_ARG;
    if (ack_len < NX_SESSION_ACK_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    /* Extract Bob's ephemeral pubkey */
    memcpy(s->dh_remote, ack_payload, NX_PUBKEY_SIZE);

    /* DH ratchet: DH(our_eph, their_eph) -> recv chain */
    uint8_t dh_out[32];
    dh(s->dh_secret, s->dh_remote, dh_out);

    uint8_t new_root[32];
    kdf_rk(s->root_key, dh_out, new_root, s->recv_chain.key);
    memcpy(s->root_key, new_root, 32);
    s->recv_chain.n = 0;
    crypto_wipe(dh_out, 32);
    crypto_wipe(new_root, 32);

    /* Generate new DH keypair for sending, ratchet again */
    nx_err_t err = generate_dh(s->dh_secret, s->dh_public);
    if (err != NX_OK) return err;

    dh(s->dh_secret, s->dh_remote, dh_out);
    kdf_rk(s->root_key, dh_out, new_root, s->send_chain.key);
    memcpy(s->root_key, new_root, 32);
    s->send_chain.n = 0;
    crypto_wipe(dh_out, 32);
    crypto_wipe(new_root, 32);

    s->established = true;
    return NX_OK;
}

/* ── Encrypt ─────────────────────────────────────────────────────────── */

nx_err_t nx_session_encrypt(nx_session_t *s,
                            const uint8_t *plaintext, size_t plaintext_len,
                            uint8_t *out_buf, size_t out_buf_len,
                            size_t *out_len)
{
    if (!s || !out_buf || !out_len) return NX_ERR_INVALID_ARG;
    if (!s->established) return NX_ERR_INVALID_ARG;
    if (plaintext_len > 0 && !plaintext) return NX_ERR_INVALID_ARG;

    size_t needed = NX_SESSION_OVERHEAD + plaintext_len;
    if (out_buf_len < needed) return NX_ERR_BUFFER_TOO_SMALL;

    /* Derive message key and advance chain */
    uint8_t msg_key[32];
    uint8_t next_chain[32];
    kdf_ck(s->send_chain.key, next_chain, msg_key);
    memcpy(s->send_chain.key, next_chain, 32);
    crypto_wipe(next_chain, 32);

    /* Write header: [msg_num(4)][prev_n(4)][DH_pub(32)] */
    uint8_t *p = out_buf;
    write_be32(p, s->send_chain.n);      p += 4;
    write_be32(p, s->prev_send_n);       p += 4;
    memcpy(p, s->dh_public, 32);         p += 32;

    /* Generate nonce */
    uint8_t *nonce = p;                  p += NX_NONCE_SIZE;
    nx_err_t err = nx_platform_random(nonce, NX_NONCE_SIZE);
    if (err != NX_OK) {
        crypto_wipe(msg_key, 32);
        return err;
    }

    /* Encrypt with AD = header (msg_num + prev_n + DH_pub = 40 bytes) */
    uint8_t *mac = p;                    p += NX_MAC_SIZE;
    uint8_t *ct  = p;

    err = nx_crypto_aead_lock(msg_key, nonce, out_buf, 40,
                              plaintext, plaintext_len, ct, mac);
    crypto_wipe(msg_key, 32);
    if (err != NX_OK) return err;

    s->send_chain.n++;
    *out_len = needed;
    return NX_OK;
}

/* ── Skipped Keys ────────────────────────────────────────────────────── */

static void skip_message_keys(nx_session_t *s, uint32_t until)
{
    if (until <= s->recv_chain.n) return;  /* nothing to skip */
    uint32_t to_skip = until - s->recv_chain.n;
    if (to_skip > NX_SESSION_MAX_SKIP) to_skip = NX_SESSION_MAX_SKIP;

    for (uint32_t i = 0; i < to_skip; i++) {
        /* Find a free slot */
        int slot = -1;
        for (int j = 0; j < NX_SESSION_MAX_SKIP; j++) {
            if (!s->skipped[j].valid) { slot = j; break; }
        }
        if (slot < 0) {
            /* Evict oldest (slot 0), shift down */
            slot = 0;
        }

        uint8_t next_chain[32];
        kdf_ck(s->recv_chain.key, next_chain, s->skipped[slot].msg_key);
        memcpy(s->recv_chain.key, next_chain, 32);
        crypto_wipe(next_chain, 32);

        memcpy(s->skipped[slot].dh_pub, s->dh_remote, NX_PUBKEY_SIZE);
        s->skipped[slot].n = s->recv_chain.n;
        s->skipped[slot].valid = true;
        s->recv_chain.n++;
    }
}

static nx_skipped_key_t *find_skipped(nx_session_t *s,
                                      const uint8_t dh_pub[32], uint32_t n)
{
    for (int i = 0; i < NX_SESSION_MAX_SKIP; i++) {
        if (s->skipped[i].valid &&
            s->skipped[i].n == n &&
            crypto_verify32(s->skipped[i].dh_pub, dh_pub) == 0) {
            return &s->skipped[i];
        }
    }
    return NULL;
}

/* ── Decrypt ─────────────────────────────────────────────────────────── */

nx_err_t nx_session_decrypt(nx_session_t *s,
                            const uint8_t *in_buf, size_t in_len,
                            uint8_t *plaintext, size_t plaintext_buf_len,
                            size_t *plaintext_len)
{
    if (!s || !in_buf || !plaintext || !plaintext_len)
        return NX_ERR_INVALID_ARG;
    if (!s->established) return NX_ERR_INVALID_ARG;
    if (in_len < NX_SESSION_OVERHEAD) return NX_ERR_BUFFER_TOO_SMALL;

    size_t ct_len = in_len - NX_SESSION_OVERHEAD;
    if (plaintext_buf_len < ct_len) return NX_ERR_BUFFER_TOO_SMALL;

    /* Parse header */
    const uint8_t *p = in_buf;
    uint32_t msg_num = read_be32(p);     p += 4;
    /* uint32_t prev_n = read_be32(p); */ p += 4;
    const uint8_t *dh_pub = p;           p += 32;
    const uint8_t *nonce  = p;           p += NX_NONCE_SIZE;
    const uint8_t *mac    = p;           p += NX_MAC_SIZE;
    const uint8_t *ct     = p;

    /* Check skipped keys first */
    nx_skipped_key_t *sk = find_skipped(s, dh_pub, msg_num);
    if (sk) {
        nx_err_t err = nx_crypto_aead_unlock(sk->msg_key, nonce, mac,
                                             in_buf, 40, ct, ct_len,
                                             plaintext);
        crypto_wipe(sk->msg_key, 32);
        sk->valid = false;
        if (err != NX_OK) return err;
        *plaintext_len = ct_len;
        return NX_OK;
    }

    /* Check if DH ratchet needs to advance (new remote DH key) */
    if (crypto_verify32(dh_pub, s->dh_remote) != 0) {
        /* Skip any remaining messages in the current recv chain */
        skip_message_keys(s, s->recv_chain.n); /* flush current chain */

        /* Save prev chain length */
        s->prev_send_n = s->send_chain.n;

        /* Update remote DH key */
        memcpy(s->dh_remote, dh_pub, 32);

        /* DH ratchet for receiving */
        uint8_t dh_out[32];
        dh(s->dh_secret, s->dh_remote, dh_out);
        uint8_t new_root[32];
        kdf_rk(s->root_key, dh_out, new_root, s->recv_chain.key);
        memcpy(s->root_key, new_root, 32);
        s->recv_chain.n = 0;
        crypto_wipe(dh_out, 32);
        crypto_wipe(new_root, 32);

        /* Generate new DH keypair for sending */
        generate_dh(s->dh_secret, s->dh_public);

        dh(s->dh_secret, s->dh_remote, dh_out);
        kdf_rk(s->root_key, dh_out, new_root, s->send_chain.key);
        memcpy(s->root_key, new_root, 32);
        s->send_chain.n = 0;
        crypto_wipe(dh_out, 32);
        crypto_wipe(new_root, 32);
    }

    /* Skip ahead if msg_num > recv_chain.n */
    if (msg_num > s->recv_chain.n) {
        skip_message_keys(s, msg_num);
    }

    /* Derive this message's key */
    uint8_t msg_key[32];
    uint8_t next_chain[32];
    kdf_ck(s->recv_chain.key, next_chain, msg_key);
    memcpy(s->recv_chain.key, next_chain, 32);
    crypto_wipe(next_chain, 32);

    /* Decrypt */
    nx_err_t err = nx_crypto_aead_unlock(msg_key, nonce, mac,
                                         in_buf, 40, ct, ct_len,
                                         plaintext);
    crypto_wipe(msg_key, 32);
    if (err != NX_OK) return err;

    s->recv_chain.n++;
    *plaintext_len = ct_len;
    return NX_OK;
}

/* ── Persistence ─────────────────────────────────────────────────────── */

size_t nx_session_store_blob_max(void)
{
    return NX_SESSION_BLOB_HEADER + (size_t)NX_SESSION_MAX * sizeof(nx_session_t);
}

nx_err_t nx_session_store_serialize(const nx_session_store_t *store,
                                    uint8_t *buf, size_t buf_cap,
                                    size_t *out_len)
{
    if (!store || !buf || !out_len) return NX_ERR_INVALID_ARG;

    uint8_t valid_count = 0;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (store->sessions[i].valid) valid_count++;
    }

    size_t need = NX_SESSION_BLOB_HEADER + (size_t)valid_count * sizeof(nx_session_t);
    if (buf_cap < need) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = 'N';
    buf[1] = 'X';
    buf[2] = 'S';
    buf[3] = '1';
    buf[4] = NX_SESSION_BLOB_VERSION;
    buf[5] = valid_count;

    size_t off = NX_SESSION_BLOB_HEADER;
    for (int i = 0; i < NX_SESSION_MAX; i++) {
        if (!store->sessions[i].valid) continue;
        memcpy(buf + off, &store->sessions[i], sizeof(nx_session_t));
        off += sizeof(nx_session_t);
    }

    *out_len = off;
    return NX_OK;
}

nx_err_t nx_session_store_deserialize(nx_session_store_t *store,
                                      const uint8_t *buf, size_t len)
{
    if (!store || !buf) return NX_ERR_INVALID_ARG;
    if (len < NX_SESSION_BLOB_HEADER) return NX_ERR_INVALID_ARG;

    if (buf[0] != 'N' || buf[1] != 'X' || buf[2] != 'S' || buf[3] != '1') {
        return NX_ERR_INVALID_ARG;
    }
    if (buf[4] != NX_SESSION_BLOB_VERSION) return NX_ERR_INVALID_ARG;

    uint8_t count = buf[5];
    if (count > NX_SESSION_MAX) return NX_ERR_INVALID_ARG;

    size_t expected = NX_SESSION_BLOB_HEADER + (size_t)count * sizeof(nx_session_t);
    if (len != expected) return NX_ERR_INVALID_ARG;

    /* Reset the store before populating so a partial/corrupt load never
     * leaves stale state behind. */
    nx_session_store_init(store);

    size_t off = NX_SESSION_BLOB_HEADER;
    for (uint8_t i = 0; i < count; i++) {
        nx_session_t s;
        memcpy(&s, buf + off, sizeof(s));
        off += sizeof(s);

        if (!s.valid) continue;  /* skip slots we shouldn't have written */
        if (i >= NX_SESSION_MAX) break;
        memcpy(&store->sessions[i], &s, sizeof(s));
    }
    return NX_OK;
}
