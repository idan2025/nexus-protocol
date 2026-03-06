/*
 * NEXUS Protocol -- Session Node Integration Tests
 *
 * Tests session handshake and messaging through the node layer using
 * a simple pipe transport for Alice <-> Bob communication.
 */
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/packet.h"
#include "nexus/announce.h"
#include "nexus/session.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Pipe Transport ──────────────────────────────────────────────────── */

#define PIPE_BUF_SIZE 512

typedef struct {
    uint8_t data[PIPE_BUF_SIZE];
    size_t  len;
    bool    has_data;
} pipe_buf_t;

typedef struct {
    pipe_buf_t *send_buf;  /* Where we write when sending */
    pipe_buf_t *recv_buf;  /* Where we read when receiving */
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

/* ── Test Fixture ────────────────────────────────────────────────────── */

typedef struct {
    nx_node_t       alice;
    nx_node_t       bob;
    nx_identity_t   alice_id;
    nx_identity_t   bob_id;
    pipe_buf_t      a_to_b;       /* Alice sends -> Bob receives */
    pipe_buf_t      b_to_a;       /* Bob sends -> Alice receives */
    pipe_state_t    alice_pipe;
    pipe_state_t    bob_pipe;
    nx_transport_t  alice_transport;
    nx_transport_t  bob_transport;
    /* Session message reception tracking */
    uint8_t         session_data[256];
    size_t          session_data_len;
    nx_addr_short_t session_src;
    int             session_count;
} test_fixture_t;

static void on_session_cb(const nx_addr_short_t *src,
                           const uint8_t *data, size_t len,
                           void *user)
{
    test_fixture_t *f = (test_fixture_t *)user;
    if (len <= sizeof(f->session_data)) {
        memcpy(f->session_data, data, len);
        f->session_data_len = len;
    }
    f->session_src = *src;
    f->session_count++;
}

static void fixture_init(test_fixture_t *f)
{
    memset(f, 0, sizeof(*f));

    nx_identity_generate(&f->alice_id);
    nx_identity_generate(&f->bob_id);

    /* Pipe buffers: Alice sends to a_to_b, Bob sends to b_to_a */
    f->alice_pipe.send_buf = &f->a_to_b;
    f->alice_pipe.recv_buf = &f->b_to_a;
    f->bob_pipe.send_buf   = &f->b_to_a;
    f->bob_pipe.recv_buf   = &f->a_to_b;

    /* Transports */
    f->alice_transport.type   = NX_TRANSPORT_SERIAL;
    f->alice_transport.name   = "alice_pipe";
    f->alice_transport.ops    = &pipe_ops;
    f->alice_transport.state  = &f->alice_pipe;
    f->alice_transport.active = false;

    f->bob_transport.type   = NX_TRANSPORT_SERIAL;
    f->bob_transport.name   = "bob_pipe";
    f->bob_transport.ops    = &pipe_ops;
    f->bob_transport.state  = &f->bob_pipe;
    f->bob_transport.active = false;

    /* Register transports */
    nx_transport_registry_init();
    nx_transport_register(&f->alice_transport);
    nx_transport_register(&f->bob_transport);

    /* Init nodes with large beacon interval to avoid interference */
    nx_node_config_t alice_cfg = {
        .role = NX_ROLE_RELAY,
        .default_ttl = 7,
        .beacon_interval_ms = 999999999,
        .on_session = on_session_cb,
        .user_ctx = f,
    };
    nx_node_config_t bob_cfg = alice_cfg;

    nx_node_init_with_identity(&f->alice, &alice_cfg, &f->alice_id);
    nx_node_init_with_identity(&f->bob, &bob_cfg, &f->bob_id);

    /* Inject neighbor entries so each knows the other's X25519 pubkey */
    uint64_t now = nx_platform_time_ms();
    nx_neighbor_update(&f->alice.route_table,
                       &f->bob_id.short_addr, &f->bob_id.full_addr,
                       f->bob_id.sign_public, f->bob_id.x25519_public,
                       NX_ROLE_RELAY, 0, now);
    nx_route_update(&f->alice.route_table,
                    &f->bob_id.short_addr, &f->bob_id.short_addr,
                    1, 1, now);

    nx_neighbor_update(&f->bob.route_table,
                       &f->alice_id.short_addr, &f->alice_id.full_addr,
                       f->alice_id.sign_public, f->alice_id.x25519_public,
                       NX_ROLE_RELAY, 0, now);
    nx_route_update(&f->bob.route_table,
                    &f->alice_id.short_addr, &f->alice_id.short_addr,
                    1, 1, now);
}

static void fixture_cleanup(test_fixture_t *f)
{
    nx_node_stop(&f->alice);
    nx_node_stop(&f->bob);
    nx_transport_registry_init();
}

/* Activate only Alice's transport, deactivate Bob's */
static void activate_alice(test_fixture_t *f)
{
    f->alice_transport.active = true;
    f->bob_transport.active   = false;
}

/* Activate only Bob's transport, deactivate Alice's */
static void activate_bob(test_fixture_t *f)
{
    f->alice_transport.active = false;
    f->bob_transport.active   = true;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

static void test_session_establishment(void)
{
    TEST("session establishment via node layer");
    test_fixture_t f;
    fixture_init(&f);

    /* Alice starts session -> sends INIT via her transport */
    activate_alice(&f);
    nx_err_t err = nx_node_session_start(&f.alice, &f.bob_id.short_addr);
    ASSERT(err == NX_OK, "session_start");
    ASSERT(f.a_to_b.has_data, "INIT sent");

    /* Bob polls -> receives INIT, auto-sends ACK */
    activate_bob(&f);
    err = nx_node_poll(&f.bob, 0);
    ASSERT(err == NX_OK, "bob poll");
    ASSERT(f.b_to_a.has_data, "ACK sent");

    /* Alice polls -> receives ACK, completes session */
    activate_alice(&f);
    err = nx_node_poll(&f.alice, 0);
    ASSERT(err == NX_OK, "alice poll");

    /* Verify both sessions are established */
    nx_session_t *as = nx_session_find(&f.alice.sessions, &f.bob_id.short_addr);
    ASSERT(as != NULL, "alice session exists");
    ASSERT(as->established, "alice session established");

    nx_session_t *bs = nx_session_find(&f.bob.sessions, &f.alice_id.short_addr);
    ASSERT(bs != NULL, "bob session exists");
    ASSERT(bs->established, "bob session established");

    fixture_cleanup(&f);
    PASS();
}

static void test_session_send_recv(void)
{
    TEST("session send and receive message");
    test_fixture_t f;
    fixture_init(&f);

    /* Establish session */
    activate_alice(&f);
    nx_node_session_start(&f.alice, &f.bob_id.short_addr);
    activate_bob(&f);
    nx_node_poll(&f.bob, 0);
    activate_alice(&f);
    nx_node_poll(&f.alice, 0);

    /* Alice sends session message */
    const char *msg = "Hello secure world!";
    size_t msg_len = strlen(msg);
    activate_alice(&f);
    nx_err_t err = nx_node_send_session(&f.alice, &f.bob_id.short_addr,
                                         (const uint8_t *)msg, msg_len);
    ASSERT(err == NX_OK, "send_session");
    ASSERT(f.a_to_b.has_data, "msg sent");

    /* Bob polls -> receives and decrypts */
    f.session_count = 0;
    activate_bob(&f);
    err = nx_node_poll(&f.bob, 0);
    ASSERT(err == NX_OK, "bob poll msg");
    ASSERT(f.session_count == 1, "callback fired");
    ASSERT(f.session_data_len == msg_len, "data length");
    ASSERT(memcmp(f.session_data, msg, msg_len) == 0, "plaintext matches");

    fixture_cleanup(&f);
    PASS();
}

static void test_session_bidirectional(void)
{
    TEST("session bidirectional messages");
    test_fixture_t f;
    fixture_init(&f);

    /* Establish session */
    activate_alice(&f);
    nx_node_session_start(&f.alice, &f.bob_id.short_addr);
    activate_bob(&f);
    nx_node_poll(&f.bob, 0);
    activate_alice(&f);
    nx_node_poll(&f.alice, 0);

    /* Alice -> Bob */
    activate_alice(&f);
    nx_node_send_session(&f.alice, &f.bob_id.short_addr,
                          (const uint8_t *)"from alice", 10);
    f.session_count = 0;
    activate_bob(&f);
    nx_node_poll(&f.bob, 0);
    ASSERT(f.session_count == 1, "alice->bob delivered");
    ASSERT(f.session_data_len == 10, "a->b len");
    ASSERT(memcmp(f.session_data, "from alice", 10) == 0, "a->b data");

    /* Bob -> Alice */
    activate_bob(&f);
    nx_err_t err = nx_node_send_session(&f.bob, &f.alice_id.short_addr,
                                         (const uint8_t *)"from bob", 8);
    ASSERT(err == NX_OK, "bob send");
    f.session_count = 0;
    activate_alice(&f);
    nx_node_poll(&f.alice, 0);
    ASSERT(f.session_count == 1, "bob->alice delivered");
    ASSERT(f.session_data_len == 8, "b->a len");
    ASSERT(memcmp(f.session_data, "from bob", 8) == 0, "b->a data");

    fixture_cleanup(&f);
    PASS();
}

static void test_session_no_neighbor(void)
{
    TEST("session_start fails without neighbor");
    test_fixture_t f;
    fixture_init(&f);

    /* Unknown peer address */
    nx_addr_short_t unknown = {{0x99, 0x99, 0x99, 0x99}};
    nx_err_t err = nx_node_session_start(&f.alice, &unknown);
    ASSERT(err == NX_ERR_NOT_FOUND, "not found");

    fixture_cleanup(&f);
    PASS();
}

static void test_session_not_established(void)
{
    TEST("send_session fails without established session");
    test_fixture_t f;
    fixture_init(&f);

    /* No session exists for Bob */
    nx_err_t err = nx_node_send_session(&f.alice, &f.bob_id.short_addr,
                                         (const uint8_t *)"test", 4);
    ASSERT(err == NX_ERR_NOT_FOUND, "no session");

    fixture_cleanup(&f);
    PASS();
}

int main(void)
{
    printf("=== NEXUS Session Node Integration Tests ===\n");

    test_session_establishment();
    test_session_send_recv();
    test_session_bidirectional();
    test_session_no_neighbor();
    test_session_not_established();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
