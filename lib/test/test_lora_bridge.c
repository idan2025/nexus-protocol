/*
 * NEXUS Protocol -- LoRa Bridge Integration Test
 *
 * Full relay topology:
 *   Phone A --[BLE pipe]--> Node A --[LoRa mock]--> Node B --[BLE pipe]--> Phone B
 *
 * Verifies:
 *   1. Phone A's announce reaches Phone B's BLE pipe after two relay hops
 *   2. Phone B's announce reaches Phone A's BLE pipe (reverse direction)
 *   3. Both relay nodes record the bridged peer as a neighbor (non-empty LoRa table)
 *
 * Transport registry layout used in this file:
 *   [0]  ble_a  — BLE pipe node-side for node_a ↔ phone_a
 *   [1]  lora_a — LoRa mock transport for node_a (radio A, linked to B)
 *   [2]  lora_b — LoRa mock transport for node_b (radio B, linked to A)
 *   [3]  ble_b  — BLE pipe node-side for node_b ↔ phone_b
 *
 * During node_a polls only transports [0]+[1] are active; during node_b polls
 * only [2]+[3] are active.  This mirrors per-device transport isolation.
 *
 * Relay polling pattern (two polls per node):
 *   Poll 1: receives from ingress transport, dispatches, enqueues flood defer.
 *   flush_deferred(): sets all pending defer entries' send_after_ms = 0.
 *   Poll 2: flood_defer_tick fires (now >= 0), relay is transmitted on egress.
 * This sidesteps the T1/T2 race between the top-of-poll timestamp used by
 * flood_defer_tick and the per-dispatch timestamp stored in defer entries.
 */

#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/announce.h"
#include "nexus/packet.h"
#include "nexus/transport.h"
#include "nexus/lora_radio.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)        do { tests_run++; printf("  %-56s", name); } while (0)
#define PASS()            do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)         do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Single-slot pipe transport (same pattern as test_gateway.c) ─────────── */

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
    ps->send_buf->len      = len;
    ps->send_buf->has_data = true;
    return NX_OK;
}

static nx_err_t pipe_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    pipe_state_t *ps = (pipe_state_t *)t->state;
    if (!ps->recv_buf->has_data) { *out_len = 0; return NX_ERR_TIMEOUT; }
    if (ps->recv_buf->len > buf_len) return NX_ERR_BUFFER_TOO_SMALL;
    memcpy(buf, ps->recv_buf->data, ps->recv_buf->len);
    *out_len               = ps->recv_buf->len;
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

/* ── Defer flush helper ──────────────────────────────────────────────────── */

/* Expire all pending flood-defer entries on a node so they fire on the next
 * poll.  Called between poll 1 (receives and enqueues) and poll 2 (fires),
 * sidestepping the internal T1/T2 timestamp race. */
static void flush_deferred(nx_node_t *node)
{
    for (int i = 0; i < NX_FLOOD_DEFER_MAX; i++)
        if (node->flood_defer[i].valid)
            node->flood_defer[i].send_after_ms = 0;
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

typedef struct {
    nx_identity_t   phone_a_id, phone_b_id, node_a_id, node_b_id;

    /* BLE pipe buffers: p2n = phone→node, n2p = node→phone */
    pipe_buf_t      ble_a_p2n, ble_a_n2p;  /* phone_a ↔ node_a */
    pipe_buf_t      ble_b_p2n, ble_b_n2p;  /* phone_b ↔ node_b */
    pipe_state_t    ble_a_ps, ble_b_ps;    /* node-side pipe state */
    nx_transport_t  ble_a_t, ble_b_t;

    nx_lora_radio_t *ra, *rb;
    nx_transport_t  *lora_a, *lora_b;

    nx_node_t       node_a, node_b;

    int             na_nbr_count;
    nx_addr_short_t na_nbr_last;
    int             nb_nbr_count;
    nx_addr_short_t nb_nbr_last;
} lora_bridge_fixture_t;

static void on_nbr_a(const nx_addr_short_t *addr, nx_role_t role, void *user)
{
    lora_bridge_fixture_t *f = (lora_bridge_fixture_t *)user;
    (void)role;
    f->na_nbr_count++;
    f->na_nbr_last = *addr;
}

static void on_nbr_b(const nx_addr_short_t *addr, nx_role_t role, void *user)
{
    lora_bridge_fixture_t *f = (lora_bridge_fixture_t *)user;
    (void)role;
    f->nb_nbr_count++;
    f->nb_nbr_last = *addr;
}

static void fixture_init(lora_bridge_fixture_t *f)
{
    memset(f, 0, sizeof(*f));

    nx_identity_generate(&f->phone_a_id);
    nx_identity_generate(&f->phone_b_id);
    nx_identity_generate(&f->node_a_id);
    nx_identity_generate(&f->node_b_id);

    f->ble_a_ps = (pipe_state_t){ .send_buf = &f->ble_a_n2p, .recv_buf = &f->ble_a_p2n };
    f->ble_b_ps = (pipe_state_t){ .send_buf = &f->ble_b_n2p, .recv_buf = &f->ble_b_p2n };

    f->ble_a_t = (nx_transport_t){
        .type   = NX_TRANSPORT_BLE, .name = "ble_a",
        .ops    = &pipe_ops, .state = &f->ble_a_ps, .active = false };
    f->ble_b_t = (nx_transport_t){
        .type   = NX_TRANSPORT_BLE, .name = "ble_b",
        .ops    = &pipe_ops, .state = &f->ble_b_ps, .active = false };

    /* LoRa mock radios: transmit on A buffers in B's queue, and vice versa */
    f->ra = nx_lora_mock_create();
    f->rb = nx_lora_mock_create();
    nx_lora_mock_link(f->ra, f->rb);
    nx_lora_config_t lora_cfg = NX_LORA_CONFIG_DEFAULT;
    f->ra->ops->init(f->ra, &lora_cfg);
    f->rb->ops->init(f->rb, &lora_cfg);

    f->lora_a = nx_lora_transport_create();
    f->lora_b = nx_lora_transport_create();
    nx_lora_radio_t *rp = f->ra;
    f->lora_a->ops->init(f->lora_a, &rp);
    rp = f->rb;
    f->lora_b->ops->init(f->lora_b, &rp);
    /* lora_init sets active=true; start all transports inactive */
    f->lora_a->active = false;
    f->lora_b->active = false;

    nx_transport_registry_init();
    nx_transport_register(&f->ble_a_t);  /* idx 0 */
    nx_transport_register(f->lora_a);     /* idx 1 */
    nx_transport_register(f->lora_b);     /* idx 2 */
    nx_transport_register(&f->ble_b_t);  /* idx 3 */

    nx_node_config_t cfg_a = {
        .role               = NX_ROLE_RELAY,
        .default_ttl        = 7,
        .beacon_interval_ms = 999999999,
        .on_neighbor        = on_nbr_a,
        .user_ctx           = f,
    };
    nx_node_config_t cfg_b = cfg_a;
    cfg_b.on_neighbor = on_nbr_b;

    nx_node_init_with_identity(&f->node_a, &cfg_a, &f->node_a_id);
    nx_node_init_with_identity(&f->node_b, &cfg_b, &f->node_b_id);
}

static void fixture_cleanup(lora_bridge_fixture_t *f)
{
    nx_node_stop(&f->node_a);
    nx_node_stop(&f->node_b);
    f->lora_a->ops->destroy(f->lora_a);
    nx_platform_free(f->lora_a);
    f->lora_b->ops->destroy(f->lora_b);
    nx_platform_free(f->lora_b);
    f->ra->ops->destroy(f->ra);
    nx_platform_free(f->ra);
    f->rb->ops->destroy(f->rb);
    nx_platform_free(f->rb);
    nx_transport_registry_init();
}

/* Serialize a signed announce for `id` directly into a pipe receive buffer,
 * simulating the phone sending an announce over its BLE NUS characteristic. */
static bool inject_announce(const nx_identity_t *id, nx_role_t role,
                             uint16_t seq_id, pipe_buf_t *buf)
{
    nx_packet_t pkt;
    if (nx_announce_build_packet(id, role, 7, &pkt) != NX_OK)
        return false;
    pkt.header.seq_id = seq_id;

    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    if (n <= 0 || (size_t)n > sizeof(buf->data))
        return false;

    memcpy(buf->data, wire, (size_t)n);
    buf->len      = (size_t)n;
    buf->has_data = true;
    return true;
}

/*
 * Helper: poll a node twice with the given active transports.
 * Poll 1 receives the packet and enqueues the flood-defer.
 * flush_deferred() expires the defer so it fires in poll 2.
 * Poll 2 runs flood_defer_tick and transmits the relay.
 */
static void relay_poll(nx_node_t *node,
                       nx_transport_t *t0, nx_transport_t *t1)
{
    t0->active = true;
    t1->active = true;
    nx_node_poll(node, 0);
    flush_deferred(node);
    nx_node_poll(node, 0);
    t0->active = false;
    t1->active = false;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

/*
 * Phone A injects an announce into node_a's BLE pipe receive buffer.
 * node_a receives and relays over LoRa.  node_b receives from LoRa
 * and bridges to phone_b's BLE pipe.
 */
static void test_announce_a_to_b(void)
{
    TEST("phone_a announce relays BLE→LoRa→BLE to phone_b");
    lora_bridge_fixture_t f;
    fixture_init(&f);

    ASSERT(inject_announce(&f.phone_a_id, NX_ROLE_RELAY, 1, &f.ble_a_p2n),
           "inject phone_a announce");

    relay_poll(&f.node_a, &f.ble_a_t, f.lora_a);
    relay_poll(&f.node_b, f.lora_b,   &f.ble_b_t);

    ASSERT(f.ble_b_n2p.has_data, "relayed packet reached phone_b BLE pipe");
    ASSERT(f.na_nbr_count >= 1,  "node_a recorded phone_a as neighbor");
    ASSERT(f.nb_nbr_count >= 1,  "node_b recorded phone_a as neighbor");
    ASSERT(nx_addr_short_cmp(&f.na_nbr_last, &f.phone_a_id.short_addr) == 0,
           "node_a last neighbor is phone_a");
    ASSERT(nx_addr_short_cmp(&f.nb_nbr_last, &f.phone_a_id.short_addr) == 0,
           "node_b last neighbor is phone_a");

    nx_packet_t out;
    ASSERT(nx_packet_deserialize(f.ble_b_n2p.data, f.ble_b_n2p.len, &out) == NX_OK,
           "deserialize relayed packet");
    ASSERT(nx_addr_short_cmp(&out.header.src, &f.phone_a_id.short_addr) == 0,
           "relayed packet src == phone_a");

    fixture_cleanup(&f);
    PASS();
}

/* Same path in reverse: phone_b → node_b → LoRa → node_a → phone_a. */
static void test_announce_b_to_a(void)
{
    TEST("phone_b announce relays BLE→LoRa→BLE to phone_a (reverse)");
    lora_bridge_fixture_t f;
    fixture_init(&f);

    ASSERT(inject_announce(&f.phone_b_id, NX_ROLE_RELAY, 1, &f.ble_b_p2n),
           "inject phone_b announce");

    relay_poll(&f.node_b, f.lora_b,   &f.ble_b_t);
    relay_poll(&f.node_a, &f.ble_a_t, f.lora_a);

    ASSERT(f.ble_a_n2p.has_data, "relayed packet reached phone_a BLE pipe");
    ASSERT(f.nb_nbr_count >= 1,  "node_b recorded phone_b as neighbor");
    ASSERT(f.na_nbr_count >= 1,  "node_a recorded phone_b as neighbor");

    nx_packet_t out;
    ASSERT(nx_packet_deserialize(f.ble_a_n2p.data, f.ble_a_n2p.len, &out) == NX_OK,
           "deserialize relayed packet");
    ASSERT(nx_addr_short_cmp(&out.header.src, &f.phone_b_id.short_addr) == 0,
           "relayed packet src == phone_b");

    fixture_cleanup(&f);
    PASS();
}

/*
 * Run both directions in the same fixture to confirm both nodes end up with
 * non-empty neighbor lists — the direct analogue of acceptance criterion 3
 * ("neighbors over LoRa is non-empty on each node when a peer is in range").
 */
static void test_lora_neighbors_nonempty(void)
{
    TEST("both relay nodes have non-empty LoRa neighbor lists");
    lora_bridge_fixture_t f;
    fixture_init(&f);

    /* Forward: phone_a → node_a → LoRa → node_b */
    ASSERT(inject_announce(&f.phone_a_id, NX_ROLE_RELAY, 1, &f.ble_a_p2n), "inject a");
    relay_poll(&f.node_a, &f.ble_a_t, f.lora_a);
    relay_poll(&f.node_b, f.lora_b,   &f.ble_b_t);

    /* Reverse: phone_b → node_b → LoRa → node_a */
    ASSERT(inject_announce(&f.phone_b_id, NX_ROLE_RELAY, 2, &f.ble_b_p2n), "inject b");
    relay_poll(&f.node_b, f.lora_b,   &f.ble_b_t);
    relay_poll(&f.node_a, &f.ble_a_t, f.lora_a);

    /* Both relay nodes should now know both phones */
    ASSERT(f.na_nbr_count >= 2, "node_a has >=2 neighbors (phone_a and phone_b)");
    ASSERT(f.nb_nbr_count >= 2, "node_b has >=2 neighbors (phone_a and phone_b)");

    fixture_cleanup(&f);
    PASS();
}

int main(void)
{
    printf("LoRa bridge integration tests\n");
    test_announce_a_to_b();
    test_announce_b_to_a();
    test_lora_neighbors_nonempty();
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
