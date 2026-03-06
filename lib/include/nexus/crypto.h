/*
 * NEXUS Protocol -- Cryptographic Operations
 *
 * Mode 1 (Ephemeral): One-shot X25519 + XChaCha20-Poly1305
 * - Sender generates ephemeral X25519 keypair
 * - Shared secret = X25519(ephemeral_secret, recipient_x25519_public)
 * - Session key = BLAKE2b(shared_secret)
 * - Encrypt payload with XChaCha20-Poly1305
 * - Ephemeral public key sent with message (32 bytes prepended to payload)
 */
#ifndef NEXUS_CRYPTO_H
#define NEXUS_CRYPTO_H

#include "types.h"

/* ── Key Exchange ────────────────────────────────────────────────────── */

/*
 * Perform X25519 key exchange and derive a symmetric key.
 * out_key: 32-byte derived symmetric key
 */
nx_err_t nx_crypto_x25519_derive(const uint8_t our_secret[NX_PRIVKEY_SIZE],
                                 const uint8_t their_public[NX_PUBKEY_SIZE],
                                 uint8_t out_key[NX_SYMMETRIC_KEY_SIZE]);

/* ── AEAD Encrypt/Decrypt ────────────────────────────────────────────── */

/*
 * Encrypt plaintext with XChaCha20-Poly1305.
 * Writes (plaintext_len + NX_MAC_SIZE) bytes into ciphertext.
 * nonce must be NX_NONCE_SIZE bytes. key must be NX_SYMMETRIC_KEY_SIZE bytes.
 */
nx_err_t nx_crypto_aead_lock(const uint8_t *key,
                             const uint8_t *nonce,
                             const uint8_t *ad, size_t ad_len,
                             const uint8_t *plaintext, size_t plaintext_len,
                             uint8_t *ciphertext, uint8_t *mac);

/*
 * Decrypt ciphertext with XChaCha20-Poly1305.
 * Returns NX_ERR_AUTH_FAIL if MAC verification fails.
 */
nx_err_t nx_crypto_aead_unlock(const uint8_t *key,
                               const uint8_t *nonce,
                               const uint8_t *mac,
                               const uint8_t *ad, size_t ad_len,
                               const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext);

/* ── Ephemeral Mode (Mode 1) ─────────────────────────────────────────── */

/*
 * Encrypt a message in ephemeral mode.
 * Generates ephemeral X25519 keypair, performs key exchange with recipient,
 * encrypts plaintext.
 *
 * out_buf layout: [32-byte ephemeral pubkey][24-byte nonce][16-byte MAC][ciphertext]
 * out_len: set to total bytes written (32 + 24 + 16 + plaintext_len)
 *
 * ad/ad_len: additional authenticated data (typically serialized header)
 */
nx_err_t nx_crypto_ephemeral_encrypt(
    const uint8_t recipient_x25519_pub[NX_PUBKEY_SIZE],
    const uint8_t *ad, size_t ad_len,
    const uint8_t *plaintext, size_t plaintext_len,
    uint8_t *out_buf, size_t out_buf_len, size_t *out_len);

/*
 * Decrypt a message in ephemeral mode.
 * Extracts ephemeral pubkey, derives shared secret, decrypts.
 *
 * in_buf layout: [32-byte ephemeral pubkey][24-byte nonce][16-byte MAC][ciphertext]
 */
nx_err_t nx_crypto_ephemeral_decrypt(
    const uint8_t our_x25519_secret[NX_PRIVKEY_SIZE],
    const uint8_t *ad, size_t ad_len,
    const uint8_t *in_buf, size_t in_len,
    uint8_t *plaintext, size_t plaintext_buf_len, size_t *plaintext_len);

/* Overhead added by ephemeral encryption (pubkey + nonce + MAC). */
#define NX_CRYPTO_EPHEMERAL_OVERHEAD  (NX_PUBKEY_SIZE + NX_NONCE_SIZE + NX_MAC_SIZE)

#endif /* NEXUS_CRYPTO_H */
