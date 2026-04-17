/*
 * NEXUS Protocol -- Pillar Federation Tests
 *
 * Covers:
 *   1. Anchor msg-id derivation (stable, 8-byte BLAKE2b fingerprint).
 *   2. Anchor enumeration (list_ids, find_by_id, has_id).
 *   3. DIGEST -> FETCH -> retransmit round-trip between two PILLAR nodes
 *      linked by a pipe transport, ending with the peer anchor-storing
 *      the replayed packet via the normal offline-destination path.
 */
#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/transport.h"
#include "nexus/packet.h"
#include "nexus/anchor.h"
#include "nexus/announce.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)        do { tests_run++; printf("  %-55s", name); } while (0)
#define PASS()            do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)         do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Pipe transport (same shape as test_session_node.c) ──────────────── */

#define PIPE_BUF_SIZE 1024

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
    .init = pipe_init, .send = pipe_send,
    .recv = pipe_recv, .destroy = pipe_destroy,
};

/* ── Helpers ─────────────────────────────────────────────────────────── */

static nx_packet_t make_stored_pkt(const nx_addr_short_t *src,
                                   const nx_addr_short_t *dst,
                                   uint16_t seq, const char *body)
{
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    pkt.header.hop_count = 0;
    pkt.header.ttl = 5;
    pkt.header.src = *src;
    pkt.header.dst = *dst;
    pkt.header.seq_id = seq;
    size_t n = strlen(body);
    if (n > NX_MAX_PAYLOAD) n = NX_MAX_PAYLOAD;
    pkt.header.payload_len = (uint8_t)n;
    memcpy(pkt.payload, body, n);
    return pkt;
}

/* ── 1. msg-id is stable and content-sensitive ───────────────────────── */

static void test_msg_id_stable(void)
{
    TEST("msg_id is deterministic over identical packets");
    nx_addr_short_t s = {{0x01, 0x02, 0x03, 0x04}};
    nx_addr_short_t d = {{0xAA, 0xBB, 0xCC, 0xDD}};
    nx_packet_t a = make_stored_pkt(&s, &d, 42, "hello-world");
    nx_packet_t b = make_stored_pkt(&s, &d, 42, "hello-world");
    uint8_t ia[NX_ANCHOR_MSG_ID_SIZE], ib[NX_ANCHOR_MSG_ID_SIZE];
    nx_anchor_msg_id(&a, ia);
    nx_anchor_msg_id(&b, ib);
    ASSERT(memcmp(ia, ib, NX_ANCHOR_MSG_ID_SIZE) == 0, "ids match");

    nx_packet_t c = make_stored_pkt(&s, &d, 43, "hello-world");
    uint8_t ic[NX_ANCHOR_MSG_ID_SIZE];
    nx_anchor_msg_id(&c, ic);
    ASSERT(memcmp(ia, ic, NX_ANCHOR_MSG_ID_SIZE) != 0, "seq change -> new id");

    nx_packet_t e = make_stored_pkt(&s, &d, 42, "different-body");
    uint8_t ie[NX_ANCHOR_MSG_ID_SIZE];
    nx_anchor_msg_id(&e, ie);
    ASSERT(memcmp(ia, ie, NX_ANCHOR_MSG_ID_SIZE) != 0, "body change -> new id");
    PASS();
}

/* ── 2. list_ids / find_by_id / has_id ───────────────────────────────── */

static void test_anchor_id_enumeration(void)
{
    TEST("list_ids + find_by_id + has_id walk all valid slots");
    nx_anchor_t a;
    nx_anchor_init(&a);

    nx_addr_short_t src = {{0xCA, 0xFE, 0x00, 0x01}};
    nx_addr_short_t dst = {{0xDE, 0xAD, 0x00, 0x01}};
    nx_packet_t p1 = make_stored_pkt(&src, &dst, 1, "one");
    nx_packet_t p2 = make_stored_pkt(&src, &dst, 2, "two");
    nx_packet_t p3 = make_stored_pkt(&src, &dst, 3, "three");
    ASSERT(nx_anchor_store(&a, &p1, 1000) == NX_OK, "store 1");
    ASSERT(nx_anchor_store(&a, &p2, 1001) == NX_OK, "store 2");
    ASSERT(nx_anchor_store(&a, &p3, 1002) == NX_OK, "store 3");

    uint8_t ids[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    int n = nx_anchor_list_ids(&a, ids, NX_FED_MAX_IDS_PER_PKT);
    ASSERT(n == 3, "list_ids returns 3");

    /* Each id must resolve back to the stored packet. */
    for (int i = 0; i < n; i++) {
        ASSERT(nx_anchor_has_id(&a, ids[i]), "has_id matches listed id");
        const nx_packet_t *got = nx_anchor_find_by_id(&a, ids[i]);
        ASSERT(got != NULL, "find_by_id matches listed id");
        ASSERT(got->header.payload_len > 0, "returned packet has payload");
    }

    /* Lookup of an id we never stored must miss. */
    uint8_t bogus[NX_ANCHOR_MSG_ID_SIZE] = {0};
    ASSERT(!nx_anchor_has_id(&a, bogus), "has_id misses on unknown id");
    PASS();
}

/* ── 3. DIGEST -> FETCH -> retransmit round-trip ─────────────────────── */

typedef struct {
    int              digest_seen;
    int              fetch_seen;
    int              digest_count;
    int              fetch_count;
    uint8_t          last_digest_ids[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    uint8_t          last_fetch_ids[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    nx_node_t       *self;
    const nx_addr_short_t *peer;
} fed_tap_t;

static void on_digest_reply_with_fetch(const nx_addr_short_t *src,
                                       const uint8_t *ids, int count,
                                       void *user)
{
    (void)src;
    fed_tap_t *t = (fed_tap_t *)user;
    t->digest_seen = 1;
    t->digest_count = count;
    if (count > NX_FED_MAX_IDS_PER_PKT) count = NX_FED_MAX_IDS_PER_PKT;
    memcpy(t->last_digest_ids, ids,
           (size_t)count * NX_ANCHOR_MSG_ID_SIZE);

    /* Reply with a FETCH for anything we don't have locally. */
    uint8_t wanted[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    int want = 0;
    for (int i = 0; i < count && want < NX_FED_MAX_IDS_PER_PKT; i++) {
        const uint8_t *id = ids + (size_t)i * NX_ANCHOR_MSG_ID_SIZE;
        if (!nx_anchor_has_id(&t->self->anchor, id)) {
            memcpy(wanted[want++], id, NX_ANCHOR_MSG_ID_SIZE);
        }
    }
    if (want > 0) {
        (void)nx_node_send_federation_fetch(t->self, t->peer,
                                            (const uint8_t *)wanted, want);
    }
}

static void on_fetch_serve(const nx_addr_short_t *src,
                           const uint8_t *ids, int count, void *user)
{
    (void)src;
    fed_tap_t *t = (fed_tap_t *)user;
    t->fetch_seen = 1;
    t->fetch_count = count;
    if (count > NX_FED_MAX_IDS_PER_PKT) count = NX_FED_MAX_IDS_PER_PKT;
    memcpy(t->last_fetch_ids, ids,
           (size_t)count * NX_ANCHOR_MSG_ID_SIZE);

    for (int i = 0; i < count; i++) {
        const uint8_t *id = ids + (size_t)i * NX_ANCHOR_MSG_ID_SIZE;
        const nx_packet_t *p = nx_anchor_find_by_id(&t->self->anchor, id);
        if (p) (void)nx_node_retransmit_packet(t->self, p);
    }
}

static void test_federation_roundtrip(void)
{
    TEST("DIGEST->FETCH->retransmit syncs anchor across peers");

    /* ── Identities ───────────────────────────────────────────────── */
    nx_identity_t alice_id, bob_id, chuck_id;
    nx_identity_generate(&alice_id);
    nx_identity_generate(&bob_id);
    nx_identity_generate(&chuck_id); /* offline recipient */

    /* ── Linked pipe transports ──────────────────────────────────── */
    pipe_buf_t a_to_b; memset(&a_to_b, 0, sizeof(a_to_b));
    pipe_buf_t b_to_a; memset(&b_to_a, 0, sizeof(b_to_a));
    pipe_state_t alice_ps = { .send_buf = &a_to_b, .recv_buf = &b_to_a };
    pipe_state_t bob_ps   = { .send_buf = &b_to_a, .recv_buf = &a_to_b };
    nx_transport_t at = { .type = NX_TRANSPORT_SERIAL, .name = "alice",
                          .ops = &pipe_ops, .state = &alice_ps, .active = false };
    nx_transport_t bt = { .type = NX_TRANSPORT_SERIAL, .name = "bob",
                          .ops = &pipe_ops, .state = &bob_ps, .active = false };
    nx_transport_registry_init();
    nx_transport_register(&at);
    nx_transport_register(&bt);

    /* ── Nodes ───────────────────────────────────────────────────── */
    fed_tap_t alice_tap = {0};
    fed_tap_t bob_tap   = {0};

    nx_node_config_t acfg = {
        .role = NX_ROLE_PILLAR, .default_ttl = 7,
        .beacon_interval_ms = 999999999,
        .on_fed_digest = on_digest_reply_with_fetch,
        .on_fed_fetch  = on_fetch_serve,
        .user_ctx = &alice_tap,
    };
    nx_node_config_t bcfg = acfg;
    bcfg.user_ctx = &bob_tap;

    nx_node_t alice, bob;
    ASSERT(nx_node_init_with_identity(&alice, &acfg, &alice_id) == NX_OK,
           "alice init");
    ASSERT(nx_node_init_with_identity(&bob, &bcfg, &bob_id) == NX_OK,
           "bob init");

    alice_tap.self = &alice; alice_tap.peer = &bob_id.short_addr;
    bob_tap.self   = &bob;   bob_tap.peer   = &alice_id.short_addr;

    /* Inject neighbor entries so lookups resolve. */
    uint64_t now = nx_platform_time_ms();
    nx_neighbor_update(&alice.route_table,
                       &bob_id.short_addr, &bob_id.full_addr,
                       bob_id.sign_public, bob_id.x25519_public,
                       NX_ROLE_PILLAR, 0, now);
    nx_route_update(&alice.route_table,
                    &bob_id.short_addr, &bob_id.short_addr, 1, 1, now);
    nx_neighbor_update(&bob.route_table,
                       &alice_id.short_addr, &alice_id.full_addr,
                       alice_id.sign_public, alice_id.x25519_public,
                       NX_ROLE_PILLAR, 1, now);
    nx_route_update(&bob.route_table,
                    &alice_id.short_addr, &alice_id.short_addr, 1, 1, now);

    /* ── Store a packet for offline chuck in alice's anchor ──────── */
    nx_packet_t stored = make_stored_pkt(&alice_id.short_addr,
                                         &chuck_id.short_addr,
                                         100, "for-chuck-while-offline");
    ASSERT(nx_anchor_store(&alice.anchor, &stored, now) == NX_OK, "alice stored");
    ASSERT(nx_anchor_count(&alice.anchor) == 1, "alice anchor=1");
    ASSERT(nx_anchor_count(&bob.anchor)   == 0, "bob anchor=0");

    /* ── Alice emits DIGEST → bob receives ───────────────────────── */
    uint8_t ids[NX_FED_MAX_IDS_PER_PKT][NX_ANCHOR_MSG_ID_SIZE];
    int have = nx_anchor_list_ids(&alice.anchor, ids, NX_FED_MAX_IDS_PER_PKT);
    ASSERT(have == 1, "list_ids=1");

    at.active = true; bt.active = false;
    ASSERT(nx_node_send_federation_digest(&alice, &bob_id.short_addr,
                                          (const uint8_t *)ids, have) == NX_OK,
           "send DIGEST");
    ASSERT(a_to_b.has_data, "DIGEST on wire");

    at.active = false; bt.active = true;
    ASSERT(nx_node_poll(&bob, 0) == NX_OK, "bob poll DIGEST");
    ASSERT(bob_tap.digest_seen == 1, "bob got DIGEST");
    ASSERT(bob_tap.digest_count == 1, "DIGEST carried 1 id");
    /* Bob's callback should have emitted a FETCH for the missing id. */
    ASSERT(b_to_a.has_data, "FETCH on wire");

    /* ── Alice receives FETCH → retransmits stored packet ────────── */
    at.active = true; bt.active = false;
    ASSERT(nx_node_poll(&alice, 0) == NX_OK, "alice poll FETCH");
    ASSERT(alice_tap.fetch_seen == 1, "alice got FETCH");
    ASSERT(alice_tap.fetch_count == 1, "FETCH carried 1 id");
    ASSERT(a_to_b.has_data, "retransmitted packet on wire");

    /* ── Bob receives replay → anchor-stores for offline chuck ───── */
    at.active = false; bt.active = true;
    ASSERT(nx_node_poll(&bob, 0) == NX_OK, "bob poll replay");
    ASSERT(nx_anchor_count(&bob.anchor) == 1, "bob anchor=1 after sync");
    ASSERT(nx_anchor_count_for(&bob.anchor, &chuck_id.short_addr) == 1,
           "bob holds chuck's packet");

    nx_node_stop(&alice);
    nx_node_stop(&bob);
    nx_transport_registry_init();
    PASS();
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("Federation tests\n");
    test_msg_id_stable();
    test_anchor_id_enumeration();
    test_federation_roundtrip();
    printf("%d / %d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
