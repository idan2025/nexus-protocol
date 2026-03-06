/*
 * NEXUS Protocol -- Anchor Unit Tests
 */
#include "nexus/anchor.h"
#include "nexus/packet.h"
#include "nexus/identity.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

static nx_packet_t make_pkt(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                            const char *data)
{
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.dst = (nx_addr_short_t){{d0, d1, d2, d3}};
    pkt.header.src = (nx_addr_short_t){{0xCA, 0xFE, 0x00, 0x01}};
    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    size_t len = strlen(data);
    if (len > NX_MAX_PAYLOAD) len = NX_MAX_PAYLOAD;
    pkt.header.payload_len = (uint8_t)len;
    memcpy(pkt.payload, data, len);
    return pkt;
}

static void test_store_and_retrieve(void)
{
    TEST("store and retrieve messages");

    nx_anchor_t a;
    nx_anchor_init(&a);

    nx_packet_t p1 = make_pkt(0xDE, 0xAD, 0x00, 0x01, "hello");
    nx_packet_t p2 = make_pkt(0xDE, 0xAD, 0x00, 0x01, "world");
    nx_packet_t p3 = make_pkt(0xBE, 0xEF, 0x00, 0x01, "other");

    ASSERT(nx_anchor_store(&a, &p1, 1000) == NX_OK, "store 1");
    ASSERT(nx_anchor_store(&a, &p2, 1001) == NX_OK, "store 2");
    ASSERT(nx_anchor_store(&a, &p3, 1002) == NX_OK, "store 3");

    ASSERT(nx_anchor_count(&a) == 3, "total 3");
    nx_addr_short_t dest1 = {{0xDE, 0xAD, 0x00, 0x01}};
    ASSERT(nx_anchor_count_for(&a, &dest1) == 2, "2 for dest1");

    /* Retrieve for dest1 */
    nx_packet_t out[8];
    int n = nx_anchor_retrieve(&a, &dest1, out, 8);
    ASSERT(n == 2, "retrieved 2");
    ASSERT(memcmp(out[0].payload, "hello", 5) == 0, "msg 1");
    ASSERT(memcmp(out[1].payload, "world", 5) == 0, "msg 2");

    /* They should be removed */
    ASSERT(nx_anchor_count(&a) == 1, "1 remaining");
    ASSERT(nx_anchor_count_for(&a, &dest1) == 0, "0 for dest1");

    PASS();
}

static void test_per_dest_limit(void)
{
    TEST("per-destination limit enforced");

    nx_anchor_t a;
    nx_anchor_init(&a);

    for (int i = 0; i < NX_ANCHOR_MAX_PER_DEST; i++) {
        nx_packet_t p = make_pkt(0xAA, 0xBB, 0xCC, 0xDD, "msg");
        p.header.seq_id = (uint16_t)i;
        ASSERT(nx_anchor_store(&a, &p, 1000 + (uint64_t)i) == NX_OK, "store");
    }

    /* One more should fail */
    nx_packet_t extra = make_pkt(0xAA, 0xBB, 0xCC, 0xDD, "over");
    ASSERT(nx_anchor_store(&a, &extra, 2000) == NX_ERR_FULL, "limit");

    /* Different dest should still work */
    nx_packet_t other = make_pkt(0x11, 0x22, 0x33, 0x44, "ok");
    ASSERT(nx_anchor_store(&a, &other, 2001) == NX_OK, "diff dest");

    PASS();
}

static void test_expiry(void)
{
    TEST("messages expire after TTL");

    nx_anchor_t a;
    nx_anchor_init(&a);
    nx_anchor_set_ttl(&a, 5000); /* 5 second TTL for testing */

    nx_packet_t p = make_pkt(0x01, 0x02, 0x03, 0x04, "expire me");
    nx_anchor_store(&a, &p, 1000);
    ASSERT(nx_anchor_count(&a) == 1, "stored");

    /* Before expiry */
    nx_anchor_expire(&a, 5000);
    ASSERT(nx_anchor_count(&a) == 1, "still there");

    /* After expiry */
    nx_anchor_expire(&a, 7000);
    ASSERT(nx_anchor_count(&a) == 0, "expired");

    PASS();
}

static void test_full_mailbox(void)
{
    TEST("full mailbox returns NX_ERR_FULL");

    nx_anchor_t a;
    nx_anchor_init(&a);

    /* Fill up entirely (using multiple destinations to avoid per-dest limit) */
    for (int i = 0; i < NX_ANCHOR_MAX_STORED; i++) {
        uint8_t d = (uint8_t)(i / NX_ANCHOR_MAX_PER_DEST);
        nx_packet_t p = make_pkt(d, 0, 0, (uint8_t)(i % NX_ANCHOR_MAX_PER_DEST), "x");
        nx_err_t err = nx_anchor_store(&a, &p, 1000);
        ASSERT(err == NX_OK, "fill");
    }

    ASSERT(nx_anchor_count(&a) == NX_ANCHOR_MAX_STORED, "full");

    /* One more should fail */
    nx_packet_t extra = make_pkt(0xFF, 0xFF, 0x00, 0x00, "nope");
    ASSERT(nx_anchor_store(&a, &extra, 2000) == NX_ERR_FULL, "full err");

    PASS();
}

static void test_empty_retrieve(void)
{
    TEST("retrieve from empty mailbox returns 0");

    nx_anchor_t a;
    nx_anchor_init(&a);

    nx_addr_short_t dest = {{0x01, 0x02, 0x03, 0x04}};
    nx_packet_t out[4];
    ASSERT(nx_anchor_retrieve(&a, &dest, out, 4) == 0, "empty");

    PASS();
}

static void test_retrieve_limited(void)
{
    TEST("retrieve respects max_pkts limit");

    nx_anchor_t a;
    nx_anchor_init(&a);

    for (int i = 0; i < 5; i++) {
        nx_packet_t p = make_pkt(0xAA, 0xBB, 0x00, 0x00, "data");
        p.header.seq_id = (uint16_t)i;
        nx_anchor_store(&a, &p, 1000);
    }

    /* Retrieve only 2 */
    nx_packet_t out[2];
    int n = nx_anchor_retrieve(&a, &(nx_addr_short_t){{0xAA, 0xBB, 0x00, 0x00}},
                               out, 2);
    ASSERT(n == 2, "got 2");

    /* 3 should remain */
    ASSERT(nx_anchor_count(&a) == 3, "3 left");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Anchor Tests ===\n");

    test_store_and_retrieve();
    test_per_dest_limit();
    test_expiry();
    test_full_mailbox();
    test_empty_retrieve();
    test_retrieve_limited();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
