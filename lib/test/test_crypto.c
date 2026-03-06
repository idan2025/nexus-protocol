/*
 * NEXUS Protocol -- Crypto Unit Tests
 */
#include "nexus/crypto.h"
#include "nexus/identity.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s", name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while (0)

static void test_aead_roundtrip(void)
{
    TEST("AEAD encrypt/decrypt roundtrip");

    uint8_t key[NX_SYMMETRIC_KEY_SIZE];
    uint8_t nonce[NX_NONCE_SIZE];
    nx_platform_random(key, sizeof(key));
    nx_platform_random(nonce, sizeof(nonce));

    const char *msg = "Hello NEXUS Protocol!";
    size_t msg_len = strlen(msg);

    uint8_t ciphertext[256];
    uint8_t mac[NX_MAC_SIZE];

    nx_err_t err = nx_crypto_aead_lock(key, nonce, NULL, 0,
                                       (const uint8_t *)msg, msg_len,
                                       ciphertext, mac);
    ASSERT(err == NX_OK, "lock");

    /* Ciphertext should differ from plaintext */
    ASSERT(memcmp(ciphertext, msg, msg_len) != 0, "not encrypted");

    uint8_t decrypted[256];
    err = nx_crypto_aead_unlock(key, nonce, mac, NULL, 0,
                                ciphertext, msg_len, decrypted);
    ASSERT(err == NX_OK, "unlock");
    ASSERT(memcmp(decrypted, msg, msg_len) == 0, "plaintext mismatch");

    PASS();
}

static void test_aead_tamper_detection(void)
{
    TEST("AEAD detects tampered ciphertext");

    uint8_t key[NX_SYMMETRIC_KEY_SIZE];
    uint8_t nonce[NX_NONCE_SIZE];
    nx_platform_random(key, sizeof(key));
    nx_platform_random(nonce, sizeof(nonce));

    const char *msg = "Integrity test";
    size_t msg_len = strlen(msg);

    uint8_t ct[256];
    uint8_t mac[NX_MAC_SIZE];
    nx_crypto_aead_lock(key, nonce, NULL, 0,
                        (const uint8_t *)msg, msg_len, ct, mac);

    /* Flip a bit in ciphertext */
    ct[0] ^= 0x01;

    uint8_t decrypted[256];
    nx_err_t err = nx_crypto_aead_unlock(key, nonce, mac, NULL, 0,
                                         ct, msg_len, decrypted);
    ASSERT(err == NX_ERR_AUTH_FAIL, "should detect tamper");

    PASS();
}

static void test_aead_with_ad(void)
{
    TEST("AEAD with additional data");

    uint8_t key[NX_SYMMETRIC_KEY_SIZE];
    uint8_t nonce[NX_NONCE_SIZE];
    nx_platform_random(key, sizeof(key));
    nx_platform_random(nonce, sizeof(nonce));

    const char *msg = "Payload";
    size_t msg_len = strlen(msg);
    const char *ad = "Header data";
    size_t ad_len = strlen(ad);

    uint8_t ct[256];
    uint8_t mac[NX_MAC_SIZE];
    nx_crypto_aead_lock(key, nonce, (const uint8_t *)ad, ad_len,
                        (const uint8_t *)msg, msg_len, ct, mac);

    /* Decrypt with correct AD */
    uint8_t decrypted[256];
    nx_err_t err = nx_crypto_aead_unlock(key, nonce, mac,
                                         (const uint8_t *)ad, ad_len,
                                         ct, msg_len, decrypted);
    ASSERT(err == NX_OK, "unlock with correct AD");

    /* Decrypt with wrong AD should fail */
    const char *bad_ad = "Wrong header";
    err = nx_crypto_aead_unlock(key, nonce, mac,
                                (const uint8_t *)bad_ad, strlen(bad_ad),
                                ct, msg_len, decrypted);
    ASSERT(err == NX_ERR_AUTH_FAIL, "should fail with wrong AD");

    PASS();
}

static void test_x25519_key_exchange(void)
{
    TEST("X25519 key exchange produces shared secret");

    nx_identity_t alice, bob;
    ASSERT(nx_identity_generate(&alice) == NX_OK, "gen alice");
    ASSERT(nx_identity_generate(&bob) == NX_OK, "gen bob");

    uint8_t key_ab[NX_SYMMETRIC_KEY_SIZE];
    uint8_t key_ba[NX_SYMMETRIC_KEY_SIZE];

    ASSERT(nx_crypto_x25519_derive(alice.x25519_secret, bob.x25519_public,
                                   key_ab) == NX_OK, "derive a->b");
    ASSERT(nx_crypto_x25519_derive(bob.x25519_secret, alice.x25519_public,
                                   key_ba) == NX_OK, "derive b->a");

    /* Both sides should derive the same key */
    ASSERT(memcmp(key_ab, key_ba, NX_SYMMETRIC_KEY_SIZE) == 0,
           "shared secrets differ");

    nx_identity_wipe(&alice);
    nx_identity_wipe(&bob);
    PASS();
}

static void test_ephemeral_encrypt_decrypt(void)
{
    TEST("ephemeral mode encrypt/decrypt roundtrip");

    nx_identity_t recipient;
    ASSERT(nx_identity_generate(&recipient) == NX_OK, "gen");

    const char *msg = "Secret message via ephemeral mode";
    size_t msg_len = strlen(msg);

    /* Simulated header as AD */
    uint8_t ad[NX_HEADER_SIZE] = {0x15, 0x07, 0xDE, 0xAD, 0xBE, 0xEF,
                                   0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01, 0x20};

    uint8_t encrypted[512];
    size_t enc_len;
    nx_err_t err = nx_crypto_ephemeral_encrypt(
        recipient.x25519_public, ad, sizeof(ad),
        (const uint8_t *)msg, msg_len,
        encrypted, sizeof(encrypted), &enc_len);
    ASSERT(err == NX_OK, "encrypt");
    ASSERT(enc_len == NX_CRYPTO_EPHEMERAL_OVERHEAD + msg_len, "enc size");

    uint8_t decrypted[256];
    size_t dec_len;
    err = nx_crypto_ephemeral_decrypt(
        recipient.x25519_secret, ad, sizeof(ad),
        encrypted, enc_len,
        decrypted, sizeof(decrypted), &dec_len);
    ASSERT(err == NX_OK, "decrypt");
    ASSERT(dec_len == msg_len, "dec size");
    ASSERT(memcmp(decrypted, msg, msg_len) == 0, "plaintext");

    nx_identity_wipe(&recipient);
    PASS();
}

static void test_ephemeral_wrong_key(void)
{
    TEST("ephemeral decrypt fails with wrong key");

    nx_identity_t recipient, wrong;
    ASSERT(nx_identity_generate(&recipient) == NX_OK, "gen");
    ASSERT(nx_identity_generate(&wrong) == NX_OK, "gen wrong");

    const char *msg = "You can't read this";
    size_t msg_len = strlen(msg);

    uint8_t encrypted[512];
    size_t enc_len;
    nx_crypto_ephemeral_encrypt(
        recipient.x25519_public, NULL, 0,
        (const uint8_t *)msg, msg_len,
        encrypted, sizeof(encrypted), &enc_len);

    uint8_t decrypted[256];
    size_t dec_len;
    nx_err_t err = nx_crypto_ephemeral_decrypt(
        wrong.x25519_secret, NULL, 0,
        encrypted, enc_len,
        decrypted, sizeof(decrypted), &dec_len);
    ASSERT(err == NX_ERR_AUTH_FAIL, "should fail with wrong key");

    nx_identity_wipe(&recipient);
    nx_identity_wipe(&wrong);
    PASS();
}

static void test_ephemeral_tamper(void)
{
    TEST("ephemeral decrypt detects tampered data");

    nx_identity_t recipient;
    ASSERT(nx_identity_generate(&recipient) == NX_OK, "gen");

    const char *msg = "Tamper test";
    size_t msg_len = strlen(msg);

    uint8_t encrypted[512];
    size_t enc_len;
    nx_crypto_ephemeral_encrypt(
        recipient.x25519_public, NULL, 0,
        (const uint8_t *)msg, msg_len,
        encrypted, sizeof(encrypted), &enc_len);

    /* Tamper with ciphertext portion */
    encrypted[enc_len - 1] ^= 0xFF;

    uint8_t decrypted[256];
    size_t dec_len;
    nx_err_t err = nx_crypto_ephemeral_decrypt(
        recipient.x25519_secret, NULL, 0,
        encrypted, enc_len,
        decrypted, sizeof(decrypted), &dec_len);
    ASSERT(err == NX_ERR_AUTH_FAIL, "should detect tamper");

    nx_identity_wipe(&recipient);
    PASS();
}

int main(void)
{
    printf("=== NEXUS Crypto Tests ===\n");

    test_aead_roundtrip();
    test_aead_tamper_detection();
    test_aead_with_ad();
    test_x25519_key_exchange();
    test_ephemeral_encrypt_decrypt();
    test_ephemeral_wrong_key();
    test_ephemeral_tamper();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
