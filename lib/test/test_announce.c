/*
 * NEXUS Protocol -- Announce Unit Tests
 */
#include "nexus/announce.h"
#include "nexus/identity.h"
#include "nexus/packet.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

static void test_create_and_parse(void)
{
    TEST("create and parse announcement roundtrip");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    uint8_t payload[NX_ANNOUNCE_PAYLOAD_LEN];
    nx_err_t err = nx_announce_create(&id, NX_ROLE_RELAY, NX_ANNOUNCE_FLAG_NONE,
                                      payload, sizeof(payload));
    ASSERT(err == NX_OK, "create");

    nx_announce_t ann;
    err = nx_announce_parse(payload, sizeof(payload), &ann);
    ASSERT(err == NX_OK, "parse");

    ASSERT(memcmp(ann.sign_pubkey, id.sign_public, NX_PUBKEY_SIZE) == 0,
           "sign_pubkey mismatch");
    ASSERT(memcmp(ann.x25519_pubkey, id.x25519_public, NX_PUBKEY_SIZE) == 0,
           "x25519_pubkey mismatch");
    ASSERT(ann.role == NX_ROLE_RELAY, "role");
    ASSERT(ann.flags == NX_ANNOUNCE_FLAG_NONE, "flags");

    /* Derived addresses should match identity */
    ASSERT(nx_addr_full_cmp(&ann.full_addr, &id.full_addr) == 0, "full_addr");
    ASSERT(nx_addr_short_cmp(&ann.short_addr, &id.short_addr) == 0, "short_addr");

    nx_identity_wipe(&id);
    PASS();
}

static void test_tampered_signature(void)
{
    TEST("tampered announcement fails verification");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    uint8_t payload[NX_ANNOUNCE_PAYLOAD_LEN];
    nx_announce_create(&id, NX_ROLE_SENTINEL, 0, payload, sizeof(payload));

    /* Flip a bit in the role byte */
    payload[NX_PUBKEY_SIZE * 2] ^= 0x01;

    nx_announce_t ann;
    nx_err_t err = nx_announce_parse(payload, sizeof(payload), &ann);
    ASSERT(err == NX_ERR_AUTH_FAIL, "should detect tamper");

    nx_identity_wipe(&id);
    PASS();
}

static void test_wrong_signer(void)
{
    TEST("announcement with wrong key fails");

    nx_identity_t real, fake;
    ASSERT(nx_identity_generate(&real) == NX_OK, "gen real");
    ASSERT(nx_identity_generate(&fake) == NX_OK, "gen fake");

    uint8_t payload[NX_ANNOUNCE_PAYLOAD_LEN];
    nx_announce_create(&real, NX_ROLE_LEAF, 0, payload, sizeof(payload));

    /* Replace the pubkey with the fake identity's key (signature won't match) */
    memcpy(payload, fake.sign_public, NX_PUBKEY_SIZE);

    nx_announce_t ann;
    ASSERT(nx_announce_parse(payload, sizeof(payload), &ann) == NX_ERR_AUTH_FAIL,
           "should fail");

    nx_identity_wipe(&real);
    nx_identity_wipe(&fake);
    PASS();
}

static void test_build_packet(void)
{
    TEST("build_packet creates valid announce packet");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    nx_packet_t pkt;
    nx_err_t err = nx_announce_build_packet(&id, NX_ROLE_RELAY, 7, &pkt);
    ASSERT(err == NX_OK, "build");

    ASSERT(nx_packet_flag_ptype(pkt.header.flags) == NX_PTYPE_ANNOUNCE, "ptype");
    ASSERT(nx_packet_flag_rtype(pkt.header.flags) == NX_RTYPE_FLOOD, "rtype");
    ASSERT(pkt.header.payload_len == NX_ANNOUNCE_PAYLOAD_LEN, "len");
    ASSERT(pkt.header.ttl == 7, "ttl");

    /* Source should be our short addr */
    ASSERT(nx_addr_short_cmp(&pkt.header.src, &id.short_addr) == 0, "src");

    /* Destination should be broadcast */
    nx_addr_short_t bcast = NX_ADDR_BROADCAST_SHORT;
    ASSERT(nx_addr_short_cmp(&pkt.header.dst, &bcast) == 0, "dst broadcast");

    /* Parse the embedded announcement */
    nx_announce_t ann;
    ASSERT(nx_announce_parse(pkt.payload, pkt.header.payload_len, &ann) == NX_OK,
           "parse from packet");

    /* Can serialize/deserialize the whole packet */
    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(n > 0, "serialize");

    nx_packet_t pkt2;
    ASSERT(nx_packet_deserialize(wire, (size_t)n, &pkt2) == NX_OK, "deser");
    ASSERT(nx_announce_parse(pkt2.payload, pkt2.header.payload_len, &ann) == NX_OK,
           "parse roundtrip");

    nx_identity_wipe(&id);
    PASS();
}

static void test_different_roles(void)
{
    TEST("announcements preserve all role values");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    nx_role_t roles[] = { NX_ROLE_LEAF, NX_ROLE_RELAY, NX_ROLE_GATEWAY,
                          NX_ROLE_ANCHOR, NX_ROLE_SENTINEL };

    for (int i = 0; i < 5; i++) {
        uint8_t payload[NX_ANNOUNCE_PAYLOAD_LEN];
        nx_announce_create(&id, roles[i], 0, payload, sizeof(payload));

        nx_announce_t ann;
        ASSERT(nx_announce_parse(payload, sizeof(payload), &ann) == NX_OK, "parse");
        ASSERT(ann.role == roles[i], "role mismatch");
    }

    nx_identity_wipe(&id);
    PASS();
}

static void test_buffer_too_small(void)
{
    TEST("create rejects too-small buffer");

    nx_identity_t id;
    ASSERT(nx_identity_generate(&id) == NX_OK, "gen");

    uint8_t small[10];
    ASSERT(nx_announce_create(&id, NX_ROLE_LEAF, 0, small, sizeof(small))
           == NX_ERR_BUFFER_TOO_SMALL, "should fail");

    nx_identity_wipe(&id);
    PASS();
}

int main(void)
{
    printf("=== NEXUS Announce Tests ===\n");

    test_create_and_parse();
    test_tampered_signature();
    test_wrong_signer();
    test_build_packet();
    test_different_roles();
    test_buffer_too_small();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
