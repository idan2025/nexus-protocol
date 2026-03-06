/*
 * NEXUS Protocol -- Route Unit Tests
 */
#include "nexus/route.h"
#include "nexus/identity.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

static nx_addr_short_t make_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    nx_addr_short_t addr = {{a, b, c, d}};
    return addr;
}

/* ── Neighbor Tests ──────────────────────────────────────────────────── */

static void test_neighbor_add_find(void)
{
    TEST("add and find neighbor");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t addr = make_addr(0xAA, 0xBB, 0xCC, 0xDD);
    nx_addr_full_t full;
    memset(&full, 0x11, sizeof(full));

    uint8_t sign_pub[NX_PUBKEY_SIZE] = {0};
    uint8_t x_pub[NX_PUBKEY_SIZE] = {0};

    nx_err_t err = nx_neighbor_update(&rt, &addr, &full, sign_pub, x_pub,
                                      NX_ROLE_RELAY, -50, 1000);
    ASSERT(err == NX_OK, "update");
    ASSERT(nx_neighbor_count(&rt) == 1, "count");

    const nx_neighbor_t *n = nx_neighbor_find(&rt, &addr);
    ASSERT(n != NULL, "find");
    ASSERT(n->role == NX_ROLE_RELAY, "role");
    ASSERT(n->rssi == -50, "rssi");

    PASS();
}

static void test_neighbor_update_existing(void)
{
    TEST("updating existing neighbor refreshes fields");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t addr = make_addr(0x01, 0x02, 0x03, 0x04);

    nx_neighbor_update(&rt, &addr, NULL, NULL, NULL, NX_ROLE_LEAF, -80, 1000);
    nx_neighbor_update(&rt, &addr, NULL, NULL, NULL, NX_ROLE_RELAY, -40, 2000);

    ASSERT(nx_neighbor_count(&rt) == 1, "count should still be 1");

    const nx_neighbor_t *n = nx_neighbor_find(&rt, &addr);
    ASSERT(n != NULL, "find");
    ASSERT(n->role == NX_ROLE_RELAY, "role updated");
    ASSERT(n->rssi == -40, "rssi updated");
    ASSERT(n->last_seen_ms == 2000, "time updated");

    PASS();
}

static void test_neighbor_expire(void)
{
    TEST("expired neighbors are removed");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t addr = make_addr(0x10, 0x20, 0x30, 0x40);
    nx_neighbor_update(&rt, &addr, NULL, NULL, NULL, NX_ROLE_LEAF, 0, 1000);

    ASSERT(nx_neighbor_count(&rt) == 1, "before expire");

    /* Expire at time well past the timeout */
    nx_route_expire(&rt, 1000 + NX_NEIGHBOR_TIMEOUT_MS + 1);

    ASSERT(nx_neighbor_count(&rt) == 0, "after expire");
    ASSERT(nx_neighbor_find(&rt, &addr) == NULL, "not found");

    PASS();
}

static void test_neighbor_full_table(void)
{
    TEST("neighbor table full returns NX_ERR_FULL");

    nx_route_table_t rt;
    nx_route_init(&rt);

    for (int i = 0; i < NX_MAX_NEIGHBORS; i++) {
        nx_addr_short_t addr = make_addr(0, 0, (uint8_t)(i >> 8), (uint8_t)i);
        nx_err_t err = nx_neighbor_update(&rt, &addr, NULL, NULL, NULL,
                                          NX_ROLE_LEAF, 0, 1000);
        ASSERT(err == NX_OK, "fill");
    }

    nx_addr_short_t extra = make_addr(0xFF, 0xFF, 0xFF, 0x01);
    nx_err_t err = nx_neighbor_update(&rt, &extra, NULL, NULL, NULL,
                                      NX_ROLE_LEAF, 0, 1000);
    ASSERT(err == NX_ERR_FULL, "should be full");

    PASS();
}

/* ── Route Tests ─────────────────────────────────────────────────────── */

static void test_route_add_lookup(void)
{
    TEST("add route and look it up");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t dest = make_addr(0xDE, 0xAD, 0x00, 0x01);
    nx_addr_short_t hop  = make_addr(0xCA, 0xFE, 0x00, 0x01);

    nx_err_t err = nx_route_update(&rt, &dest, &hop, 3, 5, 1000);
    ASSERT(err == NX_OK, "update");

    const nx_route_t *r = nx_route_lookup(&rt, &dest);
    ASSERT(r != NULL, "lookup");
    ASSERT(r->hop_count == 3, "hops");
    ASSERT(r->metric == 5, "metric");
    ASSERT(nx_addr_short_cmp(&r->next_hop, &hop) == 0, "next_hop");

    PASS();
}

static void test_route_prefer_better_metric(void)
{
    TEST("route update keeps better metric");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t dest = make_addr(0x01, 0x02, 0x03, 0x04);
    nx_addr_short_t hop1 = make_addr(0xAA, 0x00, 0x00, 0x01);
    nx_addr_short_t hop2 = make_addr(0xBB, 0x00, 0x00, 0x02);

    nx_route_update(&rt, &dest, &hop1, 5, 10, 1000);
    nx_route_update(&rt, &dest, &hop2, 2, 3, 1000);  /* Better */

    const nx_route_t *r = nx_route_lookup(&rt, &dest);
    ASSERT(r != NULL, "found");
    ASSERT(r->metric == 3, "better metric kept");
    ASSERT(nx_addr_short_cmp(&r->next_hop, &hop2) == 0, "better hop kept");

    /* Worse update should NOT replace */
    nx_addr_short_t hop3 = make_addr(0xCC, 0x00, 0x00, 0x03);
    nx_route_update(&rt, &dest, &hop3, 8, 20, 1000);

    r = nx_route_lookup(&rt, &dest);
    ASSERT(r->metric == 3, "worse not replaced");

    PASS();
}

static void test_route_expire(void)
{
    TEST("expired routes are removed");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t dest = make_addr(0x11, 0x22, 0x33, 0x44);
    nx_addr_short_t hop  = make_addr(0x55, 0x66, 0x77, 0x88);

    nx_route_update(&rt, &dest, &hop, 1, 1, 1000);
    ASSERT(nx_route_lookup(&rt, &dest) != NULL, "before");

    nx_route_expire(&rt, 1000 + NX_ROUTE_TIMEOUT_MS + 1);
    ASSERT(nx_route_lookup(&rt, &dest) == NULL, "after");

    PASS();
}

static void test_route_invalidate_via(void)
{
    TEST("invalidate_via removes routes through a hop");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t hop = make_addr(0xAA, 0xBB, 0xCC, 0xDD);
    nx_addr_short_t d1  = make_addr(0x01, 0x00, 0x00, 0x00);
    nx_addr_short_t d2  = make_addr(0x02, 0x00, 0x00, 0x00);
    nx_addr_short_t d3  = make_addr(0x03, 0x00, 0x00, 0x00);
    nx_addr_short_t other_hop = make_addr(0x99, 0x00, 0x00, 0x00);

    nx_route_update(&rt, &d1, &hop, 1, 1, 1000);
    nx_route_update(&rt, &d2, &hop, 2, 2, 1000);
    nx_route_update(&rt, &d3, &other_hop, 1, 1, 1000);

    int removed = nx_route_invalidate_via(&rt, &hop);
    ASSERT(removed == 2, "removed 2");
    ASSERT(nx_route_lookup(&rt, &d1) == NULL, "d1 gone");
    ASSERT(nx_route_lookup(&rt, &d2) == NULL, "d2 gone");
    ASSERT(nx_route_lookup(&rt, &d3) != NULL, "d3 still there");

    PASS();
}

/* ── Dedup Tests ─────────────────────────────────────────────────────── */

static void test_dedup_basic(void)
{
    TEST("dedup detects duplicate packets");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t src = make_addr(0xAA, 0xBB, 0xCC, 0xDD);

    bool dup1 = nx_dedup_check(&rt, &src, 42, 1000);
    ASSERT(dup1 == false, "first is not dup");

    bool dup2 = nx_dedup_check(&rt, &src, 42, 1001);
    ASSERT(dup2 == true, "second is dup");

    /* Different seq_id is not a dup */
    bool dup3 = nx_dedup_check(&rt, &src, 43, 1002);
    ASSERT(dup3 == false, "different seq not dup");

    PASS();
}

static void test_dedup_expire(void)
{
    TEST("dedup entries expire");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t src = make_addr(0x11, 0x22, 0x33, 0x44);

    nx_dedup_check(&rt, &src, 100, 1000);

    /* After timeout, same packet should not be dup */
    nx_route_expire(&rt, 1000 + NX_DEDUP_TIMEOUT_MS + 1);

    bool dup = nx_dedup_check(&rt, &src, 100, 1000 + NX_DEDUP_TIMEOUT_MS + 2);
    ASSERT(dup == false, "expired entry not dup");

    PASS();
}

/* ── RREQ/RREP Builder Tests ────────────────────────────────────────── */

static void test_build_rreq(void)
{
    TEST("build RREQ payload");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t origin = make_addr(0x01, 0x02, 0x03, 0x04);
    nx_addr_short_t dest   = make_addr(0xDE, 0xAD, 0xBE, 0xEF);

    uint8_t buf[32];
    int n = nx_route_build_rreq(&rt, &origin, &dest, buf, sizeof(buf));
    ASSERT(n == NX_RREQ_PAYLOAD_LEN, "length");
    ASSERT(buf[0] == NX_ROUTE_SUB_RREQ, "subtype");
    ASSERT(memcmp(&buf[3], origin.bytes, 4) == 0, "origin");
    ASSERT(memcmp(&buf[7], dest.bytes, 4) == 0, "dest");
    ASSERT(buf[11] == 0, "hop starts at 0");

    PASS();
}

static void test_build_rrep(void)
{
    TEST("build RREP payload");

    nx_addr_short_t origin = make_addr(0x01, 0x02, 0x03, 0x04);
    nx_addr_short_t dest   = make_addr(0xDE, 0xAD, 0xBE, 0xEF);

    uint8_t buf[32];
    int n = nx_route_build_rrep(0x1234, &origin, &dest, 3, 5, buf, sizeof(buf));
    ASSERT(n == NX_RREP_PAYLOAD_LEN, "length");
    ASSERT(buf[0] == NX_ROUTE_SUB_RREP, "subtype");
    ASSERT(buf[11] == 3, "hop_count");
    ASSERT(buf[12] == 5, "metric");

    PASS();
}

static void test_process_rreq(void)
{
    TEST("process RREQ installs reverse route");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t origin   = make_addr(0x01, 0x02, 0x03, 0x04);
    nx_addr_short_t dest     = make_addr(0xDE, 0xAD, 0xBE, 0xEF);
    nx_addr_short_t neighbor = make_addr(0xAA, 0xBB, 0xCC, 0xDD);

    uint8_t rreq[NX_RREQ_PAYLOAD_LEN];
    nx_route_build_rreq(&rt, &origin, &dest, rreq, sizeof(rreq));

    /* Simulate: received from neighbor */
    nx_route_subtype_t sub;
    nx_err_t err = nx_route_process(&rt, &neighbor, rreq, sizeof(rreq),
                                    &sub, 1000);
    ASSERT(err == NX_OK, "process");
    ASSERT(sub == NX_ROUTE_SUB_RREQ, "subtype");

    /* Should have reverse route to origin via neighbor */
    const nx_route_t *r = nx_route_lookup(&rt, &origin);
    ASSERT(r != NULL, "reverse route");
    ASSERT(nx_addr_short_cmp(&r->next_hop, &neighbor) == 0, "via neighbor");

    PASS();
}

static void test_process_rrep(void)
{
    TEST("process RREP installs forward route");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t origin   = make_addr(0x01, 0x02, 0x03, 0x04);
    nx_addr_short_t dest     = make_addr(0xDE, 0xAD, 0xBE, 0xEF);
    nx_addr_short_t neighbor = make_addr(0xAA, 0xBB, 0xCC, 0xDD);

    uint8_t rrep[NX_RREP_PAYLOAD_LEN];
    nx_route_build_rrep(0x0001, &origin, &dest, 2, 3, rrep, sizeof(rrep));

    nx_route_subtype_t sub;
    nx_err_t err = nx_route_process(&rt, &neighbor, rrep, sizeof(rrep),
                                    &sub, 1000);
    ASSERT(err == NX_OK, "process");
    ASSERT(sub == NX_ROUTE_SUB_RREP, "subtype");

    /* Forward route to dest via neighbor */
    const nx_route_t *r = nx_route_lookup(&rt, &dest);
    ASSERT(r != NULL, "forward route");
    ASSERT(nx_addr_short_cmp(&r->next_hop, &neighbor) == 0, "via neighbor");
    ASSERT(r->hop_count == 2, "hops");

    PASS();
}

static void test_process_rerr(void)
{
    TEST("process RERR invalidates routes");

    nx_route_table_t rt;
    nx_route_init(&rt);

    nx_addr_short_t bad   = make_addr(0xBA, 0xAD, 0x00, 0x00);
    nx_addr_short_t dest  = make_addr(0xDE, 0xAD, 0x00, 0x00);
    nx_addr_short_t from  = make_addr(0x11, 0x22, 0x33, 0x44);

    /* Install route via the "bad" node */
    nx_route_update(&rt, &dest, &bad, 2, 2, 1000);
    ASSERT(nx_route_lookup(&rt, &dest) != NULL, "route exists");

    uint8_t rerr[NX_RERR_PAYLOAD_LEN];
    nx_route_build_rerr(&bad, rerr, sizeof(rerr));

    nx_route_subtype_t sub;
    nx_route_process(&rt, &from, rerr, sizeof(rerr), &sub, 1000);

    ASSERT(sub == NX_ROUTE_SUB_RERR, "subtype");
    ASSERT(nx_route_lookup(&rt, &dest) == NULL, "route removed");

    PASS();
}

static void test_build_beacon(void)
{
    TEST("build beacon payload");

    nx_route_table_t rt;
    nx_route_init(&rt);

    /* Add a couple neighbors to verify count */
    nx_addr_short_t a1 = make_addr(0x01, 0, 0, 0);
    nx_addr_short_t a2 = make_addr(0x02, 0, 0, 0);
    nx_neighbor_update(&rt, &a1, NULL, NULL, NULL, NX_ROLE_LEAF, 0, 1000);
    nx_neighbor_update(&rt, &a2, NULL, NULL, NULL, NX_ROLE_LEAF, 0, 1000);

    uint8_t buf[16];
    int n = nx_route_build_beacon(NX_ROLE_RELAY, &rt, buf, sizeof(buf));
    ASSERT(n == NX_BEACON_PAYLOAD_LEN, "length");
    ASSERT(buf[0] == NX_ROUTE_SUB_BEACON, "subtype");
    ASSERT(buf[1] == NX_ROLE_RELAY, "role");
    ASSERT(buf[2] == 2, "neighbor count");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Route Tests ===\n");

    test_neighbor_add_find();
    test_neighbor_update_existing();
    test_neighbor_expire();
    test_neighbor_full_table();
    test_route_add_lookup();
    test_route_prefer_better_metric();
    test_route_expire();
    test_route_invalidate_via();
    test_dedup_basic();
    test_dedup_expire();
    test_build_rreq();
    test_build_rrep();
    test_process_rreq();
    test_process_rrep();
    test_process_rerr();
    test_build_beacon();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
