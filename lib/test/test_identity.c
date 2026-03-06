/*
 * NEXUS Protocol -- Identity Unit Tests
 */
#include "nexus/identity.h"
#include "nexus/platform.h"

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

static void test_generate_identity(void)
{
    TEST("nx_identity_generate produces valid identity");

    nx_identity_t id;
    nx_err_t err = nx_identity_generate(&id);
    ASSERT(err == NX_OK, "generate failed");

    /* Public keys should not be all zeros */
    uint8_t zeros[NX_PUBKEY_SIZE] = {0};
    ASSERT(memcmp(id.sign_public, zeros, NX_PUBKEY_SIZE) != 0,
           "sign_public is all zeros");
    ASSERT(memcmp(id.x25519_public, zeros, NX_PUBKEY_SIZE) != 0,
           "x25519_public is all zeros");

    /* Addresses should not be all zeros */
    uint8_t z4[NX_SHORT_ADDR_SIZE] = {0};
    uint8_t z16[NX_FULL_ADDR_SIZE] = {0};
    ASSERT(memcmp(id.full_addr.bytes, z16, NX_FULL_ADDR_SIZE) != 0,
           "full_addr is all zeros");
    ASSERT(memcmp(id.short_addr.bytes, z4, NX_SHORT_ADDR_SIZE) != 0,
           "short_addr is all zeros");

    nx_identity_wipe(&id);
    PASS();
}

static void test_two_identities_differ(void)
{
    TEST("two generated identities are distinct");

    nx_identity_t a, b;
    ASSERT(nx_identity_generate(&a) == NX_OK, "gen a");
    ASSERT(nx_identity_generate(&b) == NX_OK, "gen b");

    ASSERT(memcmp(a.sign_public, b.sign_public, NX_PUBKEY_SIZE) != 0,
           "sign_public identical");
    ASSERT(nx_addr_full_cmp(&a.full_addr, &b.full_addr) != 0,
           "full_addr identical");

    nx_identity_wipe(&a);
    nx_identity_wipe(&b);
    PASS();
}

static void test_short_addr_from_full(void)
{
    TEST("short addr is first 4 bytes of full addr");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    ASSERT(memcmp(id.short_addr.bytes, id.full_addr.bytes,
                  NX_SHORT_ADDR_SIZE) == 0,
           "short != prefix of full");

    nx_identity_wipe(&id);
    PASS();
}

static void test_address_derivation_deterministic(void)
{
    TEST("address derivation is deterministic");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    nx_addr_full_t full2;
    nx_identity_derive_full_addr(id.sign_public, &full2);
    ASSERT(nx_addr_full_cmp(&id.full_addr, &full2) == 0,
           "re-derived full addr differs");

    nx_addr_short_t short2;
    nx_identity_derive_short_addr(&full2, &short2);
    ASSERT(nx_addr_short_cmp(&id.short_addr, &short2) == 0,
           "re-derived short addr differs");

    nx_identity_wipe(&id);
    PASS();
}

static void test_wipe_zeros_secrets(void)
{
    TEST("nx_identity_wipe zeros out secret material");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    nx_identity_wipe(&id);

    uint8_t zeros64[NX_SIGN_SECRET_SIZE] = {0};
    uint8_t zeros32[NX_PRIVKEY_SIZE] = {0};
    ASSERT(memcmp(id.sign_secret, zeros64, NX_SIGN_SECRET_SIZE) == 0,
           "sign_secret not wiped");
    ASSERT(memcmp(id.x25519_secret, zeros32, NX_PRIVKEY_SIZE) == 0,
           "x25519_secret not wiped");

    PASS();
}

static void test_null_arg(void)
{
    TEST("nx_identity_generate rejects NULL");

    nx_err_t err = nx_identity_generate(NULL);
    ASSERT(err == NX_ERR_INVALID_ARG, "should fail with INVALID_ARG");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Identity Tests ===\n");

    test_generate_identity();
    test_two_identities_differ();
    test_short_addr_from_full();
    test_address_derivation_deterministic();
    test_wipe_zeros_secrets();
    test_null_arg();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
