/*
 * NEXUS Protocol -- Session (Double Ratchet) Unit Tests
 */
#include "nexus/session.h"
#include "nexus/identity.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* Helper: perform full handshake between Alice and Bob sessions */
static nx_err_t do_handshake(nx_session_t *alice, nx_session_t *bob,
                             const nx_identity_t *id_a,
                             const nx_identity_t *id_b)
{
    uint8_t init_payload[NX_SESSION_INIT_LEN];
    uint8_t ack_payload[NX_SESSION_ACK_LEN];
    nx_err_t err;

    err = nx_session_initiate(alice,
                              id_a->x25519_secret, id_a->x25519_public,
                              id_b->x25519_public,
                              init_payload, sizeof(init_payload));
    if (err != NX_OK) return err;

    err = nx_session_accept(bob,
                            id_b->x25519_secret, id_b->x25519_public,
                            id_a->x25519_public,
                            init_payload, sizeof(init_payload),
                            ack_payload, sizeof(ack_payload));
    if (err != NX_OK) return err;

    err = nx_session_complete(alice, ack_payload, sizeof(ack_payload));
    return err;
}

static void test_store_alloc_find(void)
{
    TEST("session store alloc and find");

    nx_session_store_t store;
    nx_session_store_init(&store);

    nx_addr_short_t peer = {{0xDE, 0xAD, 0xBE, 0xEF}};
    nx_session_t *s = nx_session_alloc(&store, &peer);
    ASSERT(s != NULL, "alloc");
    ASSERT(s->valid, "valid");
    ASSERT(nx_session_count(&store) == 1, "count");

    nx_session_t *found = nx_session_find(&store, &peer);
    ASSERT(found == s, "found same");

    /* Alloc again returns same */
    nx_session_t *s2 = nx_session_alloc(&store, &peer);
    ASSERT(s2 == s, "same slot");
    ASSERT(nx_session_count(&store) == 1, "still 1");

    /* Different peer */
    nx_addr_short_t peer2 = {{0xCA, 0xFE, 0x00, 0x01}};
    nx_session_t *s3 = nx_session_alloc(&store, &peer2);
    ASSERT(s3 != NULL && s3 != s, "different slot");
    ASSERT(nx_session_count(&store) == 2, "count 2");

    PASS();
}

static void test_store_remove(void)
{
    TEST("session store remove wipes keys");

    nx_session_store_t store;
    nx_session_store_init(&store);

    nx_addr_short_t peer = {{0x01, 0x02, 0x03, 0x04}};
    nx_session_alloc(&store, &peer);
    ASSERT(nx_session_count(&store) == 1, "count");

    nx_session_remove(&store, &peer);
    ASSERT(nx_session_count(&store) == 0, "removed");
    ASSERT(nx_session_find(&store, &peer) == NULL, "not found");

    PASS();
}

static void test_handshake(void)
{
    TEST("handshake establishes session");

    nx_identity_t alice_id, bob_id;
    ASSERT(nx_identity_generate(&alice_id) == NX_OK, "gen alice");
    ASSERT(nx_identity_generate(&bob_id) == NX_OK, "gen bob");

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = true;
    bob_s.valid = true;

    ASSERT(do_handshake(&alice_s, &bob_s, &alice_id, &bob_id) == NX_OK,
           "handshake");
    ASSERT(alice_s.established, "alice established");
    ASSERT(bob_s.established, "bob established");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_send_recv_roundtrip(void)
{
    TEST("encrypt/decrypt roundtrip");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    /* Alice sends to Bob */
    const char *msg = "Hello Bob, from Alice!";
    size_t msg_len = strlen(msg);

    uint8_t ct[512];
    size_t ct_len;
    ASSERT(nx_session_encrypt(&alice_s, (const uint8_t *)msg, msg_len,
                              ct, sizeof(ct), &ct_len) == NX_OK, "encrypt");
    ASSERT(ct_len == NX_SESSION_OVERHEAD + msg_len, "ct size");

    uint8_t pt[256];
    size_t pt_len;
    ASSERT(nx_session_decrypt(&bob_s, ct, ct_len,
                              pt, sizeof(pt), &pt_len) == NX_OK, "decrypt");
    ASSERT(pt_len == msg_len, "pt size");
    ASSERT(memcmp(pt, msg, msg_len) == 0, "plaintext matches");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_bidirectional(void)
{
    TEST("bidirectional messages");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    uint8_t ct[512], pt[256];
    size_t ct_len, pt_len;

    /* Alice -> Bob */
    nx_session_encrypt(&alice_s, (const uint8_t *)"msg1", 4, ct, sizeof(ct), &ct_len);
    ASSERT(nx_session_decrypt(&bob_s, ct, ct_len, pt, sizeof(pt), &pt_len) == NX_OK, "1");
    ASSERT(memcmp(pt, "msg1", 4) == 0, "1 data");

    /* Bob -> Alice */
    nx_session_encrypt(&bob_s, (const uint8_t *)"msg2", 4, ct, sizeof(ct), &ct_len);
    ASSERT(nx_session_decrypt(&alice_s, ct, ct_len, pt, sizeof(pt), &pt_len) == NX_OK, "2");
    ASSERT(memcmp(pt, "msg2", 4) == 0, "2 data");

    /* Alice -> Bob again */
    nx_session_encrypt(&alice_s, (const uint8_t *)"msg3", 4, ct, sizeof(ct), &ct_len);
    ASSERT(nx_session_decrypt(&bob_s, ct, ct_len, pt, sizeof(pt), &pt_len) == NX_OK, "3");
    ASSERT(memcmp(pt, "msg3", 4) == 0, "3 data");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_forward_secrecy(void)
{
    TEST("forward secrecy (old ct fails with new keys)");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    /* Alice sends message 1 */
    uint8_t ct1[512];
    size_t ct1_len;
    nx_session_encrypt(&alice_s, (const uint8_t *)"secret1", 7, ct1, sizeof(ct1), &ct1_len);

    /* Bob decrypts message 1 (advancing his ratchet) */
    uint8_t pt[256];
    size_t pt_len;
    ASSERT(nx_session_decrypt(&bob_s, ct1, ct1_len, pt, sizeof(pt), &pt_len) == NX_OK, "dec1");

    /* Bob sends a reply (advancing DH ratchet) */
    uint8_t ct2[512];
    size_t ct2_len;
    nx_session_encrypt(&bob_s, (const uint8_t *)"reply", 5, ct2, sizeof(ct2), &ct2_len);
    nx_session_decrypt(&alice_s, ct2, ct2_len, pt, sizeof(pt), &pt_len);

    /* Alice sends another message (new DH ratchet) */
    uint8_t ct3[512];
    size_t ct3_len;
    nx_session_encrypt(&alice_s, (const uint8_t *)"secret2", 7, ct3, sizeof(ct3), &ct3_len);

    /* Trying to decrypt ct1 again with Bob's current state should fail
       (chain has advanced, old message key consumed) */
    ASSERT(nx_session_decrypt(&bob_s, ct1, ct1_len, pt, sizeof(pt), &pt_len)
           != NX_OK, "old ct should fail");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_multiple_messages_same_direction(void)
{
    TEST("multiple messages same direction");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    uint8_t ct[5][512];
    size_t ct_len[5];

    /* Alice sends 5 messages without Bob replying */
    for (int i = 0; i < 5; i++) {
        char msg[16];
        int n = snprintf(msg, sizeof(msg), "msg%d", i);
        ASSERT(nx_session_encrypt(&alice_s, (const uint8_t *)msg, (size_t)n,
                                  ct[i], sizeof(ct[i]), &ct_len[i]) == NX_OK,
               "encrypt");
    }

    /* Bob decrypts all 5 in order */
    for (int i = 0; i < 5; i++) {
        uint8_t pt[256];
        size_t pt_len;
        char expected[16];
        int n = snprintf(expected, sizeof(expected), "msg%d", i);
        ASSERT(nx_session_decrypt(&bob_s, ct[i], ct_len[i],
                                  pt, sizeof(pt), &pt_len) == NX_OK, "decrypt");
        ASSERT(pt_len == (size_t)n, "len");
        ASSERT(memcmp(pt, expected, (size_t)n) == 0, "data");
    }

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_tamper_detection(void)
{
    TEST("tampered ciphertext detected");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    uint8_t ct[512];
    size_t ct_len;
    nx_session_encrypt(&alice_s, (const uint8_t *)"test", 4, ct, sizeof(ct), &ct_len);

    /* Flip a byte in the ciphertext portion */
    ct[ct_len - 1] ^= 0xFF;

    uint8_t pt[256];
    size_t pt_len;
    ASSERT(nx_session_decrypt(&bob_s, ct, ct_len, pt, sizeof(pt), &pt_len)
           == NX_ERR_AUTH_FAIL, "tamper detected");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_unique_ciphertexts(void)
{
    TEST("same plaintext produces different ciphertexts");

    nx_identity_t alice_id, bob_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);

    nx_session_t alice_s, bob_s;
    memset(&alice_s, 0, sizeof(alice_s));
    memset(&bob_s, 0, sizeof(bob_s));
    alice_s.valid = bob_s.valid = true;
    do_handshake(&alice_s, &bob_s, &alice_id, &bob_id);

    uint8_t ct1[512], ct2[512];
    size_t ct1_len, ct2_len;

    nx_session_encrypt(&alice_s, (const uint8_t *)"same", 4, ct1, sizeof(ct1), &ct1_len);
    nx_session_encrypt(&alice_s, (const uint8_t *)"same", 4, ct2, sizeof(ct2), &ct2_len);

    /* Ciphertexts must differ (different nonce + different chain key) */
    ASSERT(ct1_len == ct2_len, "same length");
    ASSERT(memcmp(ct1, ct2, ct1_len) != 0, "ciphertexts differ");

    nx_identity_wipe(&alice_id);
    nx_identity_wipe(&bob_id);
    PASS();
}

static void test_unestablished_session_rejected(void)
{
    TEST("encrypt/decrypt rejected before handshake");

    nx_session_t s;
    memset(&s, 0, sizeof(s));
    s.valid = true;
    s.established = false;

    uint8_t ct[128];
    size_t ct_len;
    ASSERT(nx_session_encrypt(&s, (const uint8_t *)"x", 1, ct, sizeof(ct), &ct_len)
           == NX_ERR_INVALID_ARG, "encrypt before handshake");

    uint8_t pt[128];
    size_t pt_len;
    uint8_t dummy[NX_SESSION_OVERHEAD + 4] = {0};
    ASSERT(nx_session_decrypt(&s, dummy, sizeof(dummy), pt, sizeof(pt), &pt_len)
           == NX_ERR_INVALID_ARG, "decrypt before handshake");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Session (Double Ratchet) Tests ===\n");

    test_store_alloc_find();
    test_store_remove();
    test_handshake();
    test_send_recv_roundtrip();
    test_bidirectional();
    test_forward_secrecy();
    test_multiple_messages_same_direction();
    test_tamper_detection();
    test_unique_ciphertexts();
    test_unestablished_session_rejected();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
