/*
 * NEXUS Protocol -- Group Encryption Unit Tests
 */
#include "nexus/group.h"
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

/* Shared test data */
static const nx_addr_short_t GROUP_ID = {{0x01, 0x02, 0x03, 0x04}};
static const nx_addr_short_t ALICE_ADDR = {{0xAA, 0xBB, 0xCC, 0xDD}};
static const nx_addr_short_t BOB_ADDR   = {{0x11, 0x22, 0x33, 0x44}};

static uint8_t group_key[32];

static void make_group_key(void)
{
    nx_platform_random(group_key, sizeof(group_key));
}

/* ── Tests ───────────────────────────────────────────────────────────── */

static void test_store_create_find(void)
{
    TEST("group store create and find");
    make_group_key();

    nx_group_store_t store;
    nx_group_store_init(&store);

    nx_group_t *g = nx_group_create(&store, &GROUP_ID, group_key, &ALICE_ADDR);
    ASSERT(g != NULL, "create");
    ASSERT(g->valid, "valid");
    ASSERT(nx_group_count(&store) == 1, "count");

    nx_group_t *found = nx_group_find(&store, &GROUP_ID);
    ASSERT(found == g, "find");

    /* Unknown group */
    nx_addr_short_t other = {{0xFF, 0xFF, 0xFF, 0xFF}};
    ASSERT(nx_group_find(&store, &other) == NULL, "not found");

    PASS();
}

static void test_store_remove(void)
{
    TEST("group store remove wipes keys");
    make_group_key();

    nx_group_store_t store;
    nx_group_store_init(&store);

    nx_group_create(&store, &GROUP_ID, group_key, &ALICE_ADDR);
    ASSERT(nx_group_count(&store) == 1, "count before");

    nx_group_remove(&store, &GROUP_ID);
    ASSERT(nx_group_count(&store) == 0, "count after");
    ASSERT(nx_group_find(&store, &GROUP_ID) == NULL, "find after remove");

    PASS();
}

static void test_add_member(void)
{
    TEST("group add member");
    make_group_key();

    nx_group_store_t store;
    nx_group_store_init(&store);

    nx_group_t *g = nx_group_create(&store, &GROUP_ID, group_key, &ALICE_ADDR);

    nx_err_t err = nx_group_add_member(g, &BOB_ADDR, group_key);
    ASSERT(err == NX_OK, "add bob");

    /* Duplicate add */
    err = nx_group_add_member(g, &BOB_ADDR, group_key);
    ASSERT(err == NX_ERR_ALREADY_EXISTS, "duplicate");

    PASS();
}

static void test_encrypt_decrypt(void)
{
    TEST("group encrypt and decrypt");
    make_group_key();

    /* Alice's side: create group, add Bob as member */
    nx_group_store_t alice_store;
    nx_group_store_init(&alice_store);
    nx_group_t *alice_g = nx_group_create(&alice_store, &GROUP_ID,
                                           group_key, &ALICE_ADDR);
    nx_group_add_member(alice_g, &BOB_ADDR, group_key);

    /* Bob's side: create group, add Alice as member */
    nx_group_store_t bob_store;
    nx_group_store_init(&bob_store);
    nx_group_t *bob_g = nx_group_create(&bob_store, &GROUP_ID,
                                         group_key, &BOB_ADDR);
    nx_group_add_member(bob_g, &ALICE_ADDR, group_key);

    /* Alice encrypts */
    const char *msg = "Hello group!";
    size_t msg_len = strlen(msg);
    uint8_t ct[256];
    size_t ct_len = 0;
    nx_err_t err = nx_group_encrypt(alice_g, (const uint8_t *)msg, msg_len,
                                     ct, sizeof(ct), &ct_len);
    ASSERT(err == NX_OK, "encrypt");
    ASSERT(ct_len == NX_GROUP_OVERHEAD + msg_len, "ct length");

    /* Bob decrypts */
    uint8_t pt[256];
    size_t pt_len = 0;
    err = nx_group_decrypt(bob_g, &ALICE_ADDR, ct, ct_len,
                            pt, sizeof(pt), &pt_len);
    ASSERT(err == NX_OK, "decrypt");
    ASSERT(pt_len == msg_len, "pt length");
    ASSERT(memcmp(pt, msg, msg_len) == 0, "plaintext matches");

    PASS();
}

static void test_multiple_senders(void)
{
    TEST("group multiple senders");
    make_group_key();

    /* Alice's store */
    nx_group_store_t alice_store;
    nx_group_store_init(&alice_store);
    nx_group_t *alice_g = nx_group_create(&alice_store, &GROUP_ID,
                                           group_key, &ALICE_ADDR);
    nx_group_add_member(alice_g, &BOB_ADDR, group_key);

    /* Bob's store */
    nx_group_store_t bob_store;
    nx_group_store_init(&bob_store);
    nx_group_t *bob_g = nx_group_create(&bob_store, &GROUP_ID,
                                         group_key, &BOB_ADDR);
    nx_group_add_member(bob_g, &ALICE_ADDR, group_key);

    /* Alice -> Bob */
    uint8_t ct1[256], pt1[256];
    size_t ct1_len = 0, pt1_len = 0;
    nx_group_encrypt(alice_g, (const uint8_t *)"from alice", 10,
                      ct1, sizeof(ct1), &ct1_len);
    nx_err_t err = nx_group_decrypt(bob_g, &ALICE_ADDR, ct1, ct1_len,
                                     pt1, sizeof(pt1), &pt1_len);
    ASSERT(err == NX_OK, "alice->bob");
    ASSERT(memcmp(pt1, "from alice", 10) == 0, "a->b data");

    /* Bob -> Alice */
    uint8_t ct2[256], pt2[256];
    size_t ct2_len = 0, pt2_len = 0;
    nx_group_encrypt(bob_g, (const uint8_t *)"from bob", 8,
                      ct2, sizeof(ct2), &ct2_len);
    err = nx_group_decrypt(alice_g, &BOB_ADDR, ct2, ct2_len,
                            pt2, sizeof(pt2), &pt2_len);
    ASSERT(err == NX_OK, "bob->alice");
    ASSERT(memcmp(pt2, "from bob", 8) == 0, "b->a data");

    PASS();
}

static void test_chain_ratchet(void)
{
    TEST("group chain ratchet advances");
    make_group_key();

    nx_group_store_t alice_store, bob_store;
    nx_group_store_init(&alice_store);
    nx_group_store_init(&bob_store);

    nx_group_t *alice_g = nx_group_create(&alice_store, &GROUP_ID,
                                           group_key, &ALICE_ADDR);
    nx_group_add_member(alice_g, &BOB_ADDR, group_key);

    nx_group_t *bob_g = nx_group_create(&bob_store, &GROUP_ID,
                                         group_key, &BOB_ADDR);
    nx_group_add_member(bob_g, &ALICE_ADDR, group_key);

    uint8_t cts[5][256];
    size_t  ct_lens[5];

    /* Alice sends 5 messages */
    for (int i = 0; i < 5; i++) {
        uint8_t data = (uint8_t)('A' + i);
        nx_err_t err = nx_group_encrypt(alice_g, &data, 1,
                                         cts[i], sizeof(cts[i]), &ct_lens[i]);
        ASSERT(err == NX_OK, "encrypt");
    }

    /* All ciphertexts should be different */
    for (int i = 0; i < 4; i++) {
        ASSERT(memcmp(cts[i], cts[i+1], ct_lens[i]) != 0, "different ct");
    }

    /* msg_num should have advanced */
    ASSERT(alice_g->send_msg_num == 5, "msg_num advanced");

    /* Bob decrypts all 5 in order */
    for (int i = 0; i < 5; i++) {
        uint8_t pt[8];
        size_t pt_len = 0;
        nx_err_t err = nx_group_decrypt(bob_g, &ALICE_ADDR,
                                         cts[i], ct_lens[i],
                                         pt, sizeof(pt), &pt_len);
        ASSERT(err == NX_OK, "decrypt");
        ASSERT(pt_len == 1, "pt len");
        ASSERT(pt[0] == (uint8_t)('A' + i), "pt data");
    }

    PASS();
}

static void test_tamper_detection(void)
{
    TEST("group tamper detection");
    make_group_key();

    nx_group_store_t alice_store, bob_store;
    nx_group_store_init(&alice_store);
    nx_group_store_init(&bob_store);

    nx_group_t *alice_g = nx_group_create(&alice_store, &GROUP_ID,
                                           group_key, &ALICE_ADDR);
    nx_group_t *bob_g = nx_group_create(&bob_store, &GROUP_ID,
                                         group_key, &BOB_ADDR);
    nx_group_add_member(bob_g, &ALICE_ADDR, group_key);

    uint8_t ct[256];
    size_t ct_len = 0;
    nx_group_encrypt(alice_g, (const uint8_t *)"test", 4,
                      ct, sizeof(ct), &ct_len);

    /* Tamper with ciphertext (last byte) */
    ct[ct_len - 1] ^= 0xFF;

    uint8_t pt[256];
    size_t pt_len = 0;
    nx_err_t err = nx_group_decrypt(bob_g, &ALICE_ADDR, ct, ct_len,
                                     pt, sizeof(pt), &pt_len);
    ASSERT(err == NX_ERR_AUTH_FAIL, "tampered fails");

    PASS();
}

static void test_wrong_group_key(void)
{
    TEST("group wrong key fails");
    make_group_key();

    /* Alice creates with real key */
    nx_group_store_t alice_store;
    nx_group_store_init(&alice_store);
    nx_group_t *alice_g = nx_group_create(&alice_store, &GROUP_ID,
                                           group_key, &ALICE_ADDR);

    /* Bob creates with wrong key */
    uint8_t wrong_key[32];
    nx_platform_random(wrong_key, sizeof(wrong_key));
    nx_group_store_t bob_store;
    nx_group_store_init(&bob_store);
    nx_group_t *bob_g = nx_group_create(&bob_store, &GROUP_ID,
                                         wrong_key, &BOB_ADDR);
    nx_group_add_member(bob_g, &ALICE_ADDR, wrong_key);

    uint8_t ct[256];
    size_t ct_len = 0;
    nx_group_encrypt(alice_g, (const uint8_t *)"secret", 6,
                      ct, sizeof(ct), &ct_len);

    uint8_t pt[256];
    size_t pt_len = 0;
    nx_err_t err = nx_group_decrypt(bob_g, &ALICE_ADDR, ct, ct_len,
                                     pt, sizeof(pt), &pt_len);
    ASSERT(err == NX_ERR_AUTH_FAIL, "wrong key fails");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Group Encryption Tests ===\n");

    test_store_create_find();
    test_store_remove();
    test_add_member();
    test_encrypt_decrypt();
    test_multiple_senders();
    test_chain_ratchet();
    test_tamper_detection();
    test_wrong_group_key();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
