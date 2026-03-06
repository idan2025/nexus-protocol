/*
 * NEXUS Protocol -- Cryptographic Operations
 *
 * Uses Monocypher for all crypto primitives:
 * - XChaCha20-Poly1305 for AEAD
 * - X25519 for key exchange
 * - BLAKE2b for key derivation
 */
#include "nexus/crypto.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>

nx_err_t nx_crypto_x25519_derive(const uint8_t our_secret[NX_PRIVKEY_SIZE],
                                 const uint8_t their_public[NX_PUBKEY_SIZE],
                                 uint8_t out_key[NX_SYMMETRIC_KEY_SIZE])
{
    if (!our_secret || !their_public || !out_key) return NX_ERR_INVALID_ARG;

    /* Raw X25519 shared secret */
    uint8_t raw_shared[32];
    crypto_x25519(raw_shared, our_secret, their_public);

    /* Derive symmetric key via BLAKE2b */
    crypto_blake2b(out_key, NX_SYMMETRIC_KEY_SIZE,
                   raw_shared, sizeof(raw_shared));

    crypto_wipe(raw_shared, sizeof(raw_shared));
    return NX_OK;
}

nx_err_t nx_crypto_aead_lock(const uint8_t *key,
                             const uint8_t *nonce,
                             const uint8_t *ad, size_t ad_len,
                             const uint8_t *plaintext, size_t plaintext_len,
                             uint8_t *ciphertext, uint8_t *mac)
{
    if (!key || !nonce || !ciphertext || !mac) return NX_ERR_INVALID_ARG;
    if (plaintext_len > 0 && !plaintext) return NX_ERR_INVALID_ARG;

    crypto_aead_lock(ciphertext, mac, key, nonce, ad, ad_len,
                     plaintext, plaintext_len);
    return NX_OK;
}

nx_err_t nx_crypto_aead_unlock(const uint8_t *key,
                               const uint8_t *nonce,
                               const uint8_t *mac,
                               const uint8_t *ad, size_t ad_len,
                               const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext)
{
    if (!key || !nonce || !mac || !plaintext) return NX_ERR_INVALID_ARG;
    if (ciphertext_len > 0 && !ciphertext) return NX_ERR_INVALID_ARG;

    int ret = crypto_aead_unlock(plaintext, mac, key, nonce, ad, ad_len,
                                 ciphertext, ciphertext_len);
    return (ret == 0) ? NX_OK : NX_ERR_AUTH_FAIL;
}

nx_err_t nx_crypto_ephemeral_encrypt(
    const uint8_t recipient_x25519_pub[NX_PUBKEY_SIZE],
    const uint8_t *ad, size_t ad_len,
    const uint8_t *plaintext, size_t plaintext_len,
    uint8_t *out_buf, size_t out_buf_len, size_t *out_len)
{
    if (!recipient_x25519_pub || !out_buf || !out_len)
        return NX_ERR_INVALID_ARG;
    if (plaintext_len > 0 && !plaintext)
        return NX_ERR_INVALID_ARG;

    size_t needed = NX_CRYPTO_EPHEMERAL_OVERHEAD + plaintext_len;
    if (out_buf_len < needed) return NX_ERR_BUFFER_TOO_SMALL;

    /* Generate ephemeral X25519 keypair */
    uint8_t eph_secret[NX_PRIVKEY_SIZE];
    nx_err_t err = nx_platform_random(eph_secret, sizeof(eph_secret));
    if (err != NX_OK) return err;

    uint8_t *eph_pub = out_buf;  /* First 32 bytes */
    crypto_x25519_public_key(eph_pub, eph_secret);

    /* Derive session key */
    uint8_t session_key[NX_SYMMETRIC_KEY_SIZE];
    err = nx_crypto_x25519_derive(eph_secret, recipient_x25519_pub, session_key);
    crypto_wipe(eph_secret, sizeof(eph_secret));
    if (err != NX_OK) {
        crypto_wipe(session_key, sizeof(session_key));
        return err;
    }

    /* Generate random nonce */
    uint8_t *nonce = out_buf + NX_PUBKEY_SIZE;  /* Next 24 bytes */
    err = nx_platform_random(nonce, NX_NONCE_SIZE);
    if (err != NX_OK) {
        crypto_wipe(session_key, sizeof(session_key));
        return err;
    }

    /* Encrypt: layout is [eph_pub(32)][nonce(24)][mac(16)][ciphertext] */
    uint8_t *mac        = out_buf + NX_PUBKEY_SIZE + NX_NONCE_SIZE;
    uint8_t *ciphertext = mac + NX_MAC_SIZE;

    err = nx_crypto_aead_lock(session_key, nonce, ad, ad_len,
                              plaintext, plaintext_len,
                              ciphertext, mac);
    crypto_wipe(session_key, sizeof(session_key));
    if (err != NX_OK) return err;

    *out_len = needed;
    return NX_OK;
}

nx_err_t nx_crypto_ephemeral_decrypt(
    const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
    const uint8_t *ad, size_t ad_len,
    const uint8_t *in_buf, size_t in_len,
    uint8_t *plaintext, size_t plaintext_buf_len, size_t *plaintext_len)
{
    if (!our_x25519_secret || !in_buf || !plaintext || !plaintext_len)
        return NX_ERR_INVALID_ARG;

    if (in_len < NX_CRYPTO_EPHEMERAL_OVERHEAD)
        return NX_ERR_BUFFER_TOO_SMALL;

    size_t ct_len = in_len - NX_CRYPTO_EPHEMERAL_OVERHEAD;
    if (plaintext_buf_len < ct_len)
        return NX_ERR_BUFFER_TOO_SMALL;

    /* Parse: [eph_pub(32)][nonce(24)][mac(16)][ciphertext] */
    const uint8_t *eph_pub    = in_buf;
    const uint8_t *nonce      = in_buf + NX_PUBKEY_SIZE;
    const uint8_t *mac        = nonce + NX_NONCE_SIZE;
    const uint8_t *ciphertext = mac + NX_MAC_SIZE;

    /* Derive session key */
    uint8_t session_key[NX_SYMMETRIC_KEY_SIZE];
    nx_err_t err = nx_crypto_x25519_derive(our_x25519_secret, eph_pub,
                                           session_key);
    if (err != NX_OK) return err;

    /* Decrypt and verify */
    err = nx_crypto_aead_unlock(session_key, nonce, mac, ad, ad_len,
                                ciphertext, ct_len, plaintext);
    crypto_wipe(session_key, sizeof(session_key));
    if (err != NX_OK) return err;

    *plaintext_len = ct_len;
    return NX_OK;
}
