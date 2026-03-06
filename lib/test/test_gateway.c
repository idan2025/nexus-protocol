/*
 * NEXUS Protocol -- Gateway + Group Node Integration Tests
 *
 * Tests gateway cross-transport bridging and group messaging
 * through the node layer using pipe transports.
 */
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/packet.h"
#include "nexus/group.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Pipe Transport (reused from test_session_node.c) ────────────────── */

#define PIPE_BUF_SIZE 512

typedef struct {
    uint8_t data[PIPE_BUF_SIZE];
    size_t  len;
    bool    has_data;
} pipe_buf_t;

typedef struct {
    pipe_buf_t *send_buf;
    pipe_buf_t *recv_buf;
} pipe_state_t;

static nx_err_t pipe_init(nx_transport_t *t, const void *config)
{
    (void)config;
    t->active = true;
    return NX_OK;
}

static nx_err_t pipe_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    pipe_state_t *ps = (pipe_state_t *)t->state;
    if (len > PIPE_BUF_SIZE) return NX_ERR_BUFFER_TOO_SMALL;
    memcpy(ps->send_buf->data, data, len);
    ps->send_buf->len = len;
    ps->send_buf->has_data = true;
    return NX_OK;
}

static nx_err_t pipe_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    pipe_state_t *ps = (pipe_state_t *)t->state;
    if (!ps->recv_buf->has_data) {
        *out_len = 0;
        return NX_ERR_TIMEOUT;
    }
    if (ps->recv_buf->len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;
    memcpy(buf, ps->recv_buf->data, ps->recv_buf->len);
    *out_len = ps->recv_buf->len;
    ps->recv_buf->has_data = false;
    return NX_OK;
}

static void pipe_destroy(nx_transport_t *t) { (void)t; }

static const nx_transport_ops_t pipe_ops = {
    .init    = pipe_init,
    .send    = pipe_send,
    .recv    = pipe_recv,
    .destroy = pipe_destroy,
};

/* ── Gateway Test Fixture (3 nodes: Alice <-> Gateway <-> Bob) ───────── */
/*
 * Transport layout:
 *   Transport 0: Alice pipe (alice_send -> gw_recv_a)
 *   Transport 1: Gateway pipe A (gw_send_a -> alice_recv)
 *   Transport 2: Gateway pipe B (gw_send_b -> bob_recv)
 *   Transport 3: Bob pipe (bob_send -> gw_recv_b)
 *
 * But since the global registry is shared, we control which are active.
 *
 * Simplified: we use 4 pipe buffers for the 2 links.
 *   Link A: alice <-> gateway (a_to_gw, gw_to_a)
 *   Link B: gateway <-> bob (gw_to_b, b_to_gw)
 */

typedef struct {
    nx_node_t       alice;
    nx_node_t       gateway;
    nx_node_t       bob;
    nx_identity_t   alice_id;
    nx_identity_t   gw_id;
    nx_identity_t   bob_id;
    /* Link A: Alice <-> Gateway */
    pipe_buf_t      a_to_gw;
    pipe_buf_t      gw_to_a;
    pipe_state_t    alice_pipe;
    pipe_state_t    gw_pipe_a;
    nx_transport_t  alice_transport;
    nx_transport_t  gw_transport_a;
    /* Link B: Gateway <-> Bob */
    pipe_buf_t      gw_to_b;
    pipe_buf_t      b_to_gw;
    pipe_state_t    gw_pipe_b;
    pipe_state_t    bob_pipe;
    nx_transport_t  gw_transport_b;
    nx_transport_t  bob_transport;
    /* Callback tracking */
    uint8_t         recv_data[256];
    size_t          recv_data_len;
    nx_addr_short_t recv_src;
    int             recv_count;
} gw_fixture_t;

static void on_data_gw(const nx_addr_short_t *src,
                        const uint8_t *data, size_t len,
                        void *user)
{
    gw_fixture_t *f = (gw_fixture_t *)user;
    if (len <= sizeof(f->recv_data)) {
        memcpy(f->recv_data, data, len);
        f->recv_data_len = len;
    }
    f->recv_src = *src;
    f->recv_count++;
}

static void gw_fixture_init(gw_fixture_t *f, nx_role_t gw_role)
{
    memset(f, 0, sizeof(*f));

    nx_identity_generate(&f->alice_id);
    nx_identity_generate(&f->gw_id);
    nx_identity_generate(&f->bob_id);

    /* Link A pipes */
    f->alice_pipe.send_buf = &f->a_to_gw;
    f->alice_pipe.recv_buf = &f->gw_to_a;
    f->gw_pipe_a.send_buf  = &f->gw_to_a;
    f->gw_pipe_a.recv_buf  = &f->a_to_gw;

    /* Link B pipes */
    f->gw_pipe_b.send_buf = &f->gw_to_b;
    f->gw_pipe_b.recv_buf = &f->b_to_gw;
    f->bob_pipe.send_buf  = &f->b_to_gw;
    f->bob_pipe.recv_buf  = &f->gw_to_b;

    /* Transport instances */
    f->alice_transport = (nx_transport_t){
        .type = NX_TRANSPORT_SERIAL, .name = "alice_pipe",
        .ops = &pipe_ops, .state = &f->alice_pipe, .active = false
    };
    f->gw_transport_a = (nx_transport_t){
        .type = NX_TRANSPORT_SERIAL, .name = "gw_pipe_a",
        .ops = &pipe_ops, .state = &f->gw_pipe_a, .active = false
    };
    f->gw_transport_b = (nx_transport_t){
        .type = NX_TRANSPORT_TCP, .name = "gw_pipe_b",
        .ops = &pipe_ops, .state = &f->gw_pipe_b, .active = false
    };
    f->bob_transport = (nx_transport_t){
        .type = NX_TRANSPORT_TCP, .name = "bob_pipe",
        .ops = &pipe_ops, .state = &f->bob_pipe, .active = false
    };

    /* Register transports -- order matters for index */
    nx_transport_registry_init();
    nx_transport_register(&f->alice_transport);   /* idx 0 */
    nx_transport_register(&f->gw_transport_a);    /* idx 1 */
    nx_transport_register(&f->gw_transport_b);    /* idx 2 */
    nx_transport_register(&f->bob_transport);     /* idx 3 */

    /* Init nodes */
    nx_node_config_t alice_cfg = {
        .role = NX_ROLE_LEAF,
        .default_ttl = 7,
        .beacon_interval_ms = 999999999,
        .on_data = on_data_gw,
        .user_ctx = f,
    };
    nx_node_config_t gw_cfg = {
        .role = gw_role,
        .default_ttl = 7,
        .beacon_interval_ms = 999999999,
        .on_data = on_data_gw,
        .user_ctx = f,
    };
    nx_node_config_t bob_cfg = alice_cfg;

    nx_node_init_with_identity(&f->alice, &alice_cfg, &f->alice_id);
    nx_node_init_with_identity(&f->gateway, &gw_cfg, &f->gw_id);
    nx_node_init_with_identity(&f->bob, &bob_cfg, &f->bob_id);

    /* Inject neighbor/route entries so gateway knows both Alice and Bob */
    uint64_t now = nx_platform_time_ms();

    /* Gateway knows Alice */
    nx_neighbor_update(&f->gateway.route_table,
                       &f->alice_id.short_addr, &f->alice_id.full_addr,
                       f->alice_id.sign_public, f->alice_id.x25519_public,
                       NX_ROLE_LEAF, 0, now);
    nx_route_update(&f->gateway.route_table,
                    &f->alice_id.short_addr, &f->alice_id.short_addr,
                    1, 1, now);

    /* Gateway knows Bob */
    nx_neighbor_update(&f->gateway.route_table,
                       &f->bob_id.short_addr, &f->bob_id.full_addr,
                       f->bob_id.sign_public, f->bob_id.x25519_public,
                       NX_ROLE_LEAF, 0, now);
    nx_route_update(&f->gateway.route_table,
                    &f->bob_id.short_addr, &f->bob_id.short_addr,
                    1, 1, now);
}

static void gw_fixture_cleanup(gw_fixture_t *f)
{
    nx_node_stop(&f->alice);
    nx_node_stop(&f->gateway);
    nx_node_stop(&f->bob);
    nx_transport_registry_init();
}

/* ── Gateway Tests ───────────────────────────────────────────────────── */

static void test_gateway_forward(void)
{
    TEST("gateway forwards across transports");
    gw_fixture_t f;
    gw_fixture_init(&f, NX_ROLE_GATEWAY);

    /* Alice sends a raw packet destined for Bob via her transport */
    f.alice_transport.active = true;
    nx_err_t err = nx_node_send_raw(&f.alice, &f.bob_id.short_addr,
                                     (const uint8_t *)"hello bob", 9);
    ASSERT(err == NX_OK, "alice send");
    ASSERT(f.a_to_gw.has_data, "data in a_to_gw");
    f.alice_transport.active = false;

    /* Gateway polls: reads from transport A (idx 1), forwards to transport B (idx 2) */
    f.gw_transport_a.active = true;
    f.gw_transport_b.active = true;
    err = nx_node_poll(&f.gateway, 0);
    ASSERT(err == NX_OK, "gw poll");
    /* Gateway should have bridged to transport B */
    ASSERT(f.gw_to_b.has_data, "data bridged to B");
    f.gw_transport_a.active = false;
    f.gw_transport_b.active = false;

    /* Bob polls and receives */
    f.recv_count = 0;
    f.bob_transport.active = true;
    err = nx_node_poll(&f.bob, 0);
    ASSERT(err == NX_OK, "bob poll");
    ASSERT(f.recv_count == 1, "bob received");
    ASSERT(f.recv_data_len == 9, "data len");
    ASSERT(memcmp(f.recv_data, "hello bob", 9) == 0, "data matches");

    gw_fixture_cleanup(&f);
    PASS();
}

static void test_gateway_no_echo(void)
{
    TEST("gateway does not echo back to ingress");
    gw_fixture_t f;
    gw_fixture_init(&f, NX_ROLE_GATEWAY);

    /* Alice sends */
    f.alice_transport.active = true;
    nx_node_send_raw(&f.alice, &f.bob_id.short_addr,
                      (const uint8_t *)"test", 4);
    f.alice_transport.active = false;

    /* Gateway polls from transport A, should forward to B but NOT back to A */
    f.gw_transport_a.active = true;
    f.gw_transport_b.active = true;
    f.gw_to_a.has_data = false; /* Clear any stale data */
    nx_node_poll(&f.gateway, 0);

    /* Transport B should have data (bridged) */
    ASSERT(f.gw_to_b.has_data, "bridged to B");
    /* Transport A should NOT have data (no echo) */
    ASSERT(!f.gw_to_a.has_data, "no echo on A");

    gw_fixture_cleanup(&f);
    PASS();
}

static void test_non_gateway_no_bridge(void)
{
    TEST("RELAY node sends on all transports (no bridge)");
    gw_fixture_t f;
    gw_fixture_init(&f, NX_ROLE_RELAY);

    /* Alice sends */
    f.alice_transport.active = true;
    nx_node_send_raw(&f.alice, &f.bob_id.short_addr,
                      (const uint8_t *)"test", 4);
    f.alice_transport.active = false;

    /* RELAY gateway polls -- should forward on ALL transports (incl. ingress) */
    f.gw_transport_a.active = true;
    f.gw_transport_b.active = true;
    f.gw_to_a.has_data = false;
    nx_node_poll(&f.gateway, 0);

    /* Both transports should have data (RELAY sends on all) */
    ASSERT(f.gw_to_b.has_data, "sent on B");
    ASSERT(f.gw_to_a.has_data, "sent on A (relay, not gateway)");

    gw_fixture_cleanup(&f);
    PASS();
}

/* ── Group Node Tests (2-node setup) ─────────────────────────────────── */

typedef struct {
    nx_node_t       alice;
    nx_node_t       bob;
    nx_identity_t   alice_id;
    nx_identity_t   bob_id;
    pipe_buf_t      a_to_b;
    pipe_buf_t      b_to_a;
    pipe_state_t    alice_pipe;
    pipe_state_t    bob_pipe;
    nx_transport_t  alice_transport;
    nx_transport_t  bob_transport;
    /* Group callback tracking */
    uint8_t         group_data[256];
    size_t          group_data_len;
    nx_addr_short_t group_src;
    nx_addr_short_t group_id_recv;
    int             group_count;
} group_fixture_t;

static void on_group_cb(const nx_addr_short_t *group_id,
                         const nx_addr_short_t *src,
                         const uint8_t *data, size_t len,
                         void *user)
{
    group_fixture_t *f = (group_fixture_t *)user;
    if (len <= sizeof(f->group_data)) {
        memcpy(f->group_data, data, len);
        f->group_data_len = len;
    }
    f->group_src = *src;
    f->group_id_recv = *group_id;
    f->group_count++;
}

static const nx_addr_short_t TEST_GROUP_ID = {{0x10, 0x20, 0x30, 0x40}};
static uint8_t test_group_key[32];

static void group_fixture_init(group_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    nx_platform_random(test_group_key, sizeof(test_group_key));

    nx_identity_generate(&f->alice_id);
    nx_identity_generate(&f->bob_id);

    f->alice_pipe.send_buf = &f->a_to_b;
    f->alice_pipe.recv_buf = &f->b_to_a;
    f->bob_pipe.send_buf   = &f->b_to_a;
    f->bob_pipe.recv_buf   = &f->a_to_b;

    f->alice_transport = (nx_transport_t){
        .type = NX_TRANSPORT_SERIAL, .name = "alice_pipe",
        .ops = &pipe_ops, .state = &f->alice_pipe, .active = false
    };
    f->bob_transport = (nx_transport_t){
        .type = NX_TRANSPORT_SERIAL, .name = "bob_pipe",
        .ops = &pipe_ops, .state = &f->bob_pipe, .active = false
    };

    nx_transport_registry_init();
    nx_transport_register(&f->alice_transport);
    nx_transport_register(&f->bob_transport);

    nx_node_config_t alice_cfg = {
        .role = NX_ROLE_RELAY,
        .default_ttl = 7,
        .beacon_interval_ms = 999999999,
        .on_group = on_group_cb,
        .user_ctx = f,
    };
    nx_node_config_t bob_cfg = alice_cfg;

    nx_node_init_with_identity(&f->alice, &alice_cfg, &f->alice_id);
    nx_node_init_with_identity(&f->bob, &bob_cfg, &f->bob_id);
}

static void group_fixture_cleanup(group_fixture_t *f)
{
    nx_node_stop(&f->alice);
    nx_node_stop(&f->bob);
    nx_transport_registry_init();
}

static void test_group_send_recv(void)
{
    TEST("group send and recv via node layer");
    group_fixture_t f;
    group_fixture_init(&f);

    /* Both create the same group and add each other as members */
    nx_node_group_create(&f.alice, &TEST_GROUP_ID, test_group_key);
    nx_node_group_add_member(&f.alice, &TEST_GROUP_ID, &f.bob_id.short_addr);

    nx_node_group_create(&f.bob, &TEST_GROUP_ID, test_group_key);
    nx_node_group_add_member(&f.bob, &TEST_GROUP_ID, &f.alice_id.short_addr);

    /* Alice sends group message */
    f.alice_transport.active = true;
    nx_err_t err = nx_node_group_send(&f.alice, &TEST_GROUP_ID,
                                       (const uint8_t *)"group msg", 9);
    ASSERT(err == NX_OK, "alice group send");
    ASSERT(f.a_to_b.has_data, "data sent");
    f.alice_transport.active = false;

    /* Bob polls and receives */
    f.group_count = 0;
    f.bob_transport.active = true;
    err = nx_node_poll(&f.bob, 0);
    ASSERT(err == NX_OK, "bob poll");
    ASSERT(f.group_count == 1, "group callback fired");
    ASSERT(f.group_data_len == 9, "data len");
    ASSERT(memcmp(f.group_data, "group msg", 9) == 0, "data matches");
    ASSERT(memcmp(f.group_id_recv.bytes, TEST_GROUP_ID.bytes, 4) == 0,
           "group id matches");

    group_fixture_cleanup(&f);
    PASS();
}

static void test_group_bidirectional(void)
{
    TEST("group bidirectional messages");
    group_fixture_t f;
    group_fixture_init(&f);

    nx_node_group_create(&f.alice, &TEST_GROUP_ID, test_group_key);
    nx_node_group_add_member(&f.alice, &TEST_GROUP_ID, &f.bob_id.short_addr);

    nx_node_group_create(&f.bob, &TEST_GROUP_ID, test_group_key);
    nx_node_group_add_member(&f.bob, &TEST_GROUP_ID, &f.alice_id.short_addr);

    /* Alice -> Bob */
    f.alice_transport.active = true;
    nx_node_group_send(&f.alice, &TEST_GROUP_ID,
                        (const uint8_t *)"from alice", 10);
    f.alice_transport.active = false;

    f.group_count = 0;
    f.bob_transport.active = true;
    nx_node_poll(&f.bob, 0);
    ASSERT(f.group_count == 1, "alice->bob");
    ASSERT(memcmp(f.group_data, "from alice", 10) == 0, "a->b data");
    f.bob_transport.active = false;

    /* Bob -> Alice */
    f.bob_transport.active = true;
    nx_node_group_send(&f.bob, &TEST_GROUP_ID,
                        (const uint8_t *)"from bob", 8);
    f.bob_transport.active = false;

    f.group_count = 0;
    f.alice_transport.active = true;
    nx_node_poll(&f.alice, 0);
    ASSERT(f.group_count == 1, "bob->alice");
    ASSERT(memcmp(f.group_data, "from bob", 8) == 0, "b->a data");

    group_fixture_cleanup(&f);
    PASS();
}

static void test_group_no_group(void)
{
    TEST("group_send fails for unknown group");
    group_fixture_t f;
    group_fixture_init(&f);

    nx_addr_short_t unknown = {{0x99, 0x99, 0x99, 0x99}};
    nx_err_t err = nx_node_group_send(&f.alice, &unknown,
                                       (const uint8_t *)"test", 4);
    ASSERT(err == NX_ERR_NOT_FOUND, "not found");

    group_fixture_cleanup(&f);
    PASS();
}

static void test_group_non_member(void)
{
    TEST("non-member cannot decrypt group message");
    group_fixture_t f;
    group_fixture_init(&f);

    /* Only Alice creates the group */
    nx_node_group_create(&f.alice, &TEST_GROUP_ID, test_group_key);

    /* Bob does NOT join the group */

    /* Alice sends */
    f.alice_transport.active = true;
    nx_node_group_send(&f.alice, &TEST_GROUP_ID,
                        (const uint8_t *)"secret", 6);
    f.alice_transport.active = false;

    /* Bob polls -- should not fire group callback (no group / can't decrypt) */
    f.group_count = 0;
    f.bob_transport.active = true;
    nx_node_poll(&f.bob, 0);
    ASSERT(f.group_count == 0, "non-member no callback");

    group_fixture_cleanup(&f);
    PASS();
}

int main(void)
{
    printf("=== NEXUS Gateway + Group Node Integration Tests ===\n");

    /* Gateway tests */
    test_gateway_forward();
    test_gateway_no_echo();
    test_non_gateway_no_bridge();

    /* Group node tests */
    test_group_send_recv();
    test_group_bidirectional();
    test_group_no_group();
    test_group_non_member();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
