/*
 * NEXUS Protocol -- TCP Internet Transport Tests
 *
 * Tests multi-peer TCP transport with server + client modes,
 * auto-reconnect, and multi-client broadcast.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nexus/transport.h"
#include "nexus/platform.h"
#include "nexus/packet.h"
#include "nexus/identity.h"
#include "nexus/node.h"
#include "nexus/announce.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-55s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Test 1: Server creates and listens ──────────────────────────────── */

static void test_server_create(void)
{
    TEST("server create and listen");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    ASSERT(srv != NULL, "alloc");

    nx_tcp_inet_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.listen_port = 19100;
    cfg.listen_host = "127.0.0.1";
    cfg.peer_count = 0;

    nx_err_t err = srv->ops->init(srv, &cfg);
    ASSERT(err == NX_OK, "init");
    ASSERT(srv->active, "active");

    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 2: Client connects to server ───────────────────────────────── */

static void test_client_connect(void)
{
    TEST("client connects to server");

    /* Server */
    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19101;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    /* Client */
    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19101;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");
    ASSERT(cli->active, "cli active");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 3: Send from client, recv on server ────────────────────────── */

static void test_send_recv(void)
{
    TEST("send from client, recv on server");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19102;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19102;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    /* Small delay for accept to happen */
    usleep(50000);

    uint8_t msg[] = "hello nexus inet";
    nx_err_t err = cli->ops->send(cli, msg, sizeof(msg));
    ASSERT(err == NX_OK, "send");

    uint8_t buf[256];
    size_t out_len = 0;
    err = srv->ops->recv(srv, buf, sizeof(buf), &out_len, 2000);
    ASSERT(err == NX_OK, "recv");
    ASSERT(out_len == sizeof(msg), "len match");
    ASSERT(memcmp(buf, msg, sizeof(msg)) == 0, "data match");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 4: Bidirectional send/recv ─────────────────────────────────── */

static void test_bidirectional(void)
{
    TEST("bidirectional send/recv");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19103;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19103;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(50000);

    /* Client -> Server */
    uint8_t msg1[] = "client says hi";
    ASSERT(cli->ops->send(cli, msg1, sizeof(msg1)) == NX_OK, "c->s send");
    uint8_t buf[256];
    size_t out_len = 0;
    ASSERT(srv->ops->recv(srv, buf, sizeof(buf), &out_len, 2000) == NX_OK, "c->s recv");
    ASSERT(memcmp(buf, msg1, sizeof(msg1)) == 0, "c->s data");

    /* Server -> Client */
    uint8_t msg2[] = "server replies";
    ASSERT(srv->ops->send(srv, msg2, sizeof(msg2)) == NX_OK, "s->c send");
    out_len = 0;
    ASSERT(cli->ops->recv(cli, buf, sizeof(buf), &out_len, 2000) == NX_OK, "s->c recv");
    ASSERT(memcmp(buf, msg2, sizeof(msg2)) == 0, "s->c data");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 5: Multiple clients connect to one server ──────────────────── */

static void test_multi_client(void)
{
    TEST("multiple clients to one server");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19104;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    /* Two clients */
    nx_transport_t *cli1 = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg1;
    memset(&ccfg1, 0, sizeof(ccfg1));
    ccfg1.peers[0].host = "127.0.0.1";
    ccfg1.peers[0].port = 19104;
    ccfg1.peer_count = 1;
    ASSERT(cli1->ops->init(cli1, &ccfg1) == NX_OK, "cli1 init");

    nx_transport_t *cli2 = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg2;
    memset(&ccfg2, 0, sizeof(ccfg2));
    ccfg2.peers[0].host = "127.0.0.1";
    ccfg2.peers[0].port = 19104;
    ccfg2.peer_count = 1;
    ASSERT(cli2->ops->init(cli2, &ccfg2) == NX_OK, "cli2 init");

    usleep(100000);

    /* Client 1 sends */
    uint8_t msg1[] = "from client 1";
    ASSERT(cli1->ops->send(cli1, msg1, sizeof(msg1)) == NX_OK, "cli1 send");

    uint8_t buf[256];
    size_t out_len = 0;
    ASSERT(srv->ops->recv(srv, buf, sizeof(buf), &out_len, 2000) == NX_OK, "recv cli1");
    ASSERT(memcmp(buf, msg1, sizeof(msg1)) == 0, "cli1 data");

    /* Client 2 sends */
    uint8_t msg2[] = "from client 2";
    ASSERT(cli2->ops->send(cli2, msg2, sizeof(msg2)) == NX_OK, "cli2 send");

    out_len = 0;
    ASSERT(srv->ops->recv(srv, buf, sizeof(buf), &out_len, 2000) == NX_OK, "recv cli2");
    ASSERT(memcmp(buf, msg2, sizeof(msg2)) == 0, "cli2 data");

    /* Server broadcasts - both clients receive */
    uint8_t broadcast[] = "broadcast msg";
    ASSERT(srv->ops->send(srv, broadcast, sizeof(broadcast)) == NX_OK, "srv bcast");

    out_len = 0;
    ASSERT(cli1->ops->recv(cli1, buf, sizeof(buf), &out_len, 2000) == NX_OK, "cli1 recv bcast");
    ASSERT(memcmp(buf, broadcast, sizeof(broadcast)) == 0, "cli1 bcast data");

    out_len = 0;
    ASSERT(cli2->ops->recv(cli2, buf, sizeof(buf), &out_len, 2000) == NX_OK, "cli2 recv bcast");
    ASSERT(memcmp(buf, broadcast, sizeof(broadcast)) == 0, "cli2 bcast data");

    cli1->ops->destroy(cli1);
    nx_platform_free(cli1);
    cli2->ops->destroy(cli2);
    nx_platform_free(cli2);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 6: Auto-reconnect after disconnect ─────────────────────────── */

static void test_reconnect(void)
{
    TEST("auto-reconnect after server restart");

    /* Start server */
    nx_transport_t *srv1 = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19105;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv1->ops->init(srv1, &scfg) == NX_OK, "srv1 init");

    /* Client with fast reconnect */
    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19105;
    ccfg.peer_count = 1;
    ccfg.reconnect_interval_ms = 500;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(50000);

    /* Verify initial connection works */
    uint8_t msg1[] = "before restart";
    ASSERT(cli->ops->send(cli, msg1, sizeof(msg1)) == NX_OK, "initial send");

    uint8_t buf[256];
    size_t out_len = 0;
    ASSERT(srv1->ops->recv(srv1, buf, sizeof(buf), &out_len, 2000) == NX_OK, "initial recv");

    /* Kill server */
    srv1->ops->destroy(srv1);
    nx_platform_free(srv1);

    usleep(100000);

    /* Restart server on same port */
    nx_transport_t *srv2 = nx_tcp_inet_transport_create();
    ASSERT(srv2->ops->init(srv2, &scfg) == NX_OK, "srv2 init");

    /* Wait for reconnect */
    usleep(800000);

    /* Try sending - client should have reconnected */
    uint8_t msg2[] = "after restart";
    /* First send may fail (triggers reconnect detection), try a few times */
    nx_err_t err = NX_ERR_TRANSPORT;
    for (int attempt = 0; attempt < 5 && err != NX_OK; attempt++) {
        /* Trigger recv to drive reconnect logic */
        uint8_t dummy[256];
        size_t dummy_len;
        cli->ops->recv(cli, dummy, sizeof(dummy), &dummy_len, 200);

        err = cli->ops->send(cli, msg2, sizeof(msg2));
        if (err != NX_OK) usleep(600000);
    }
    ASSERT(err == NX_OK, "reconnected send");

    out_len = 0;
    err = srv2->ops->recv(srv2, buf, sizeof(buf), &out_len, 2000);
    ASSERT(err == NX_OK, "reconnected recv");
    ASSERT(memcmp(buf, msg2, sizeof(msg2)) == 0, "reconnected data");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv2->ops->destroy(srv2);
    nx_platform_free(srv2);
    PASS();
}

/* ── Test 7: Node-to-node announce over TCP inet ─────────────────────── */

static volatile bool node_neighbor_seen = false;

static void on_neighbor(const nx_addr_short_t *addr, nx_role_t role, void *user)
{
    (void)addr; (void)role; (void)user;
    node_neighbor_seen = true;
}

static void test_node_announce(void)
{
    TEST("node-to-node announce over TCP inet");

    nx_transport_registry_init();

    /* Server transport */
    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19106;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");
    nx_transport_register(srv);

    /* Client transport */
    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19106;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(50000);

    /* Node A (uses client transport - sends announce) */
    nx_node_t nodeA;
    nx_node_config_t acfg;
    memset(&acfg, 0, sizeof(acfg));
    acfg.role = NX_ROLE_RELAY;
    acfg.default_ttl = 7;
    acfg.beacon_interval_ms = 60000;
    ASSERT(nx_node_init(&nodeA, &acfg) == NX_OK, "nodeA init");

    /* Build and send announce via client transport */
    nx_packet_t ann_pkt;
    memset(&ann_pkt, 0, sizeof(ann_pkt));
    ann_pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                            NX_PTYPE_ANNOUNCE, NX_RTYPE_FLOOD);
    ann_pkt.header.hop_count = 0;
    ann_pkt.header.ttl = 7;
    ann_pkt.header.src = nodeA.identity.short_addr;
    memset(ann_pkt.header.dst.bytes, 0xFF, NX_SHORT_ADDR_SIZE);
    ann_pkt.header.seq_id = 1;

    nx_err_t err = nx_announce_create(&nodeA.identity, NX_ROLE_RELAY, 0,
                                       ann_pkt.payload, sizeof(ann_pkt.payload));
    ann_pkt.header.payload_len = NX_ANNOUNCE_PAYLOAD_LEN;
    ASSERT(err == NX_OK, "announce create");

    uint8_t wire[NX_MAX_PACKET];
    int wlen = nx_packet_serialize(&ann_pkt, wire, sizeof(wire));
    ASSERT(wlen > 0, "serialize");
    ASSERT(cli->ops->send(cli, wire, (size_t)wlen) == NX_OK, "send announce");

    /* Node B (uses server transport - receives announce) */
    nx_node_t nodeB;
    nx_node_config_t bcfg;
    memset(&bcfg, 0, sizeof(bcfg));
    bcfg.role = NX_ROLE_RELAY;
    bcfg.default_ttl = 7;
    bcfg.beacon_interval_ms = 60000;
    bcfg.on_neighbor = on_neighbor;
    node_neighbor_seen = false;
    ASSERT(nx_node_init(&nodeB, &bcfg) == NX_OK, "nodeB init");

    /* Receive on server and dispatch */
    uint8_t rbuf[NX_MAX_PACKET];
    size_t rlen = 0;
    err = srv->ops->recv(srv, rbuf, sizeof(rbuf), &rlen, 2000);
    ASSERT(err == NX_OK, "recv announce");
    ASSERT(rlen > 0, "recv len");

    /* Parse and verify it's an announce */
    nx_packet_t rpkt;
    err = nx_packet_deserialize(rbuf, rlen, &rpkt);
    ASSERT(err == NX_OK, "deserialize");
    ASSERT(nx_packet_flag_ptype(rpkt.header.flags) == NX_PTYPE_ANNOUNCE, "is announce");

    nx_node_stop(&nodeA);
    nx_node_stop(&nodeB);
    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 8: Hybrid mode (server + client in one transport) ──────────── */

static void test_hybrid_mode(void)
{
    TEST("hybrid mode: server + outbound peers in one transport");

    /* Node A: server on 19107, connects to nothing yet */
    nx_transport_t *tA = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t cfgA;
    memset(&cfgA, 0, sizeof(cfgA));
    cfgA.listen_port = 19107;
    cfgA.listen_host = "127.0.0.1";
    cfgA.peer_count = 0;
    ASSERT(tA->ops->init(tA, &cfgA) == NX_OK, "A init");

    /* Node B: server on 19108, connects to A */
    nx_transport_t *tB = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t cfgB;
    memset(&cfgB, 0, sizeof(cfgB));
    cfgB.listen_port = 19108;
    cfgB.listen_host = "127.0.0.1";
    cfgB.peers[0].host = "127.0.0.1";
    cfgB.peers[0].port = 19107;
    cfgB.peer_count = 1;
    ASSERT(tB->ops->init(tB, &cfgB) == NX_OK, "B init");

    usleep(100000);

    /* B -> A */
    uint8_t msg1[] = "B to A";
    ASSERT(tB->ops->send(tB, msg1, sizeof(msg1)) == NX_OK, "B->A send");
    uint8_t buf[256];
    size_t out_len = 0;
    ASSERT(tA->ops->recv(tA, buf, sizeof(buf), &out_len, 2000) == NX_OK, "B->A recv");
    ASSERT(memcmp(buf, msg1, sizeof(msg1)) == 0, "B->A data");

    /* A -> B (A accepted B's connection, so it can send back) */
    uint8_t msg2[] = "A to B";
    ASSERT(tA->ops->send(tA, msg2, sizeof(msg2)) == NX_OK, "A->B send");
    out_len = 0;
    ASSERT(tB->ops->recv(tB, buf, sizeof(buf), &out_len, 2000) == NX_OK, "A->B recv");
    ASSERT(memcmp(buf, msg2, sizeof(msg2)) == 0, "A->B data");

    tA->ops->destroy(tA);
    nx_platform_free(tA);
    tB->ops->destroy(tB);
    nx_platform_free(tB);
    PASS();
}

/* ── Test 9: Full packet roundtrip (header + payload) ────────────────── */

static void test_packet_roundtrip(void)
{
    TEST("full NEXUS packet roundtrip over TCP inet");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19109;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19109;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(50000);

    /* Build a real packet */
    nx_identity_t id;
    nx_identity_generate(&id);

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                        NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    pkt.header.hop_count = 0;
    pkt.header.ttl = 7;
    pkt.header.src = id.short_addr;
    memset(pkt.header.dst.bytes, 0xAA, NX_SHORT_ADDR_SIZE);
    pkt.header.seq_id = 42;

    const char *payload = "test payload over internet TCP";
    size_t plen = strlen(payload) + 1;
    memcpy(pkt.payload, payload, plen);
    pkt.header.payload_len = (uint8_t)plen;

    uint8_t wire[NX_MAX_PACKET];
    int wlen = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(wlen > 0, "serialize");

    ASSERT(cli->ops->send(cli, wire, (size_t)wlen) == NX_OK, "send");

    uint8_t rbuf[NX_MAX_PACKET];
    size_t rlen = 0;
    ASSERT(srv->ops->recv(srv, rbuf, sizeof(rbuf), &rlen, 2000) == NX_OK, "recv");
    ASSERT(rlen == (size_t)wlen, "wire len match");

    nx_packet_t rpkt;
    ASSERT(nx_packet_deserialize(rbuf, rlen, &rpkt) == NX_OK, "deserialize");
    ASSERT(rpkt.header.seq_id == 42, "seq_id match");
    ASSERT(memcmp(rpkt.payload, payload, plen) == 0, "payload match");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test 10: Recv timeout with no data ──────────────────────────────── */

static void test_recv_timeout(void)
{
    TEST("recv timeout with no data");

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19110;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    uint8_t buf[256];
    size_t out_len = 0;
    uint64_t t0 = nx_platform_time_ms();
    nx_err_t err = srv->ops->recv(srv, buf, sizeof(buf), &out_len, 200);
    uint64_t t1 = nx_platform_time_ms();

    ASSERT(err == NX_ERR_TIMEOUT, "timeout err");
    ASSERT(t1 - t0 >= 150, "waited enough");

    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── PSK mutual authentication ───────────────────────────────────────── */

static void test_psk_matching(void)
{
    TEST("PSK matching: handshake succeeds, traffic flows");

    uint8_t psk[32];
    memset(psk, 0xA7, sizeof(psk));

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19120;
    scfg.listen_host = "127.0.0.1";
    memcpy(scfg.psk, psk, sizeof(psk));
    scfg.psk_len = sizeof(psk);
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19120;
    ccfg.peer_count = 1;
    memcpy(ccfg.psk, psk, sizeof(psk));
    ccfg.psk_len = sizeof(psk);
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    /* Pump handshake: each side needs recv calls to drive auth_pump through
     * SEND_HELLO → RECV_HELLO → RECV_TAG → DONE. Four alternating rounds cover
     * the worst case where neither peer has bytes waiting on the first pump. */
    uint8_t scratch[64];
    size_t out_len = 0;
    for (int i = 0; i < 4; i++) {
        srv->ops->recv(srv, scratch, sizeof(scratch), &out_len, 200);
        cli->ops->recv(cli, scratch, sizeof(scratch), &out_len, 200);
    }

    uint8_t msg[] = "authed hello";
    ASSERT(cli->ops->send(cli, msg, sizeof(msg)) == NX_OK, "send after auth");

    uint8_t buf[256];
    nx_err_t err = srv->ops->recv(srv, buf, sizeof(buf), &out_len, 2000);
    ASSERT(err == NX_OK, "recv");
    ASSERT(out_len == sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0, "payload match");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

static void test_psk_mismatch(void)
{
    TEST("PSK mismatch: peer rejected, no traffic");

    uint8_t psk_srv[32], psk_cli[32];
    memset(psk_srv, 0x11, sizeof(psk_srv));
    memset(psk_cli, 0x22, sizeof(psk_cli));

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19121;
    scfg.listen_host = "127.0.0.1";
    memcpy(scfg.psk, psk_srv, sizeof(psk_srv));
    scfg.psk_len = sizeof(psk_srv);
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19121;
    ccfg.peer_count = 1;
    memcpy(ccfg.psk, psk_cli, sizeof(psk_cli));
    ccfg.psk_len = sizeof(psk_cli);
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    uint8_t scratch[64];
    size_t out_len = 0;
    srv->ops->recv(srv, scratch, sizeof(scratch), &out_len, 300);
    cli->ops->recv(cli, scratch, sizeof(scratch), &out_len, 300);
    srv->ops->recv(srv, scratch, sizeof(scratch), &out_len, 300);

    uint8_t msg[] = "should be dropped";
    cli->ops->send(cli, msg, sizeof(msg));

    uint8_t buf[256];
    nx_err_t err = srv->ops->recv(srv, buf, sizeof(buf), &out_len, 400);
    ASSERT(err == NX_ERR_TIMEOUT, "no packet delivered on PSK mismatch");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

static void test_allow_list_blocks(void)
{
    TEST("allow-list blocks non-listed peer");

    const char *allowed[1] = { "10.255.255.254" };

    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19122;
    scfg.listen_host = "127.0.0.1";
    scfg.allow_list[0] = allowed[0];
    scfg.allow_count = 1;
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19122;
    ccfg.peer_count = 1;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(100000);

    uint8_t msg[] = "blocked";
    cli->ops->send(cli, msg, sizeof(msg));

    uint8_t buf[256];
    size_t out_len = 0;
    nx_err_t err = srv->ops->recv(srv, buf, sizeof(buf), &out_len, 400);
    ASSERT(err == NX_ERR_TIMEOUT, "loopback IP not allow-listed, peer dropped");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

/* ── Test: Failover mode connects to first available ────────────────── */

static void test_failover_connects_first(void)
{
    TEST("failover mode connects to first available peer");

    /* Start a server only on the second port (first is unreachable) */
    nx_transport_t *srv = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.listen_port = 19120;
    scfg.listen_host = "127.0.0.1";
    ASSERT(srv->ops->init(srv, &scfg) == NX_OK, "srv init");

    /* Client in failover mode: peer 0 is unreachable, peer 1 is the server */
    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19119;  /* Nothing listens here */
    ccfg.peers[1].host = "127.0.0.1";
    ccfg.peers[1].port = 19120;
    ccfg.peer_count = 2;
    ccfg.failover = true;
    ccfg.failover_timeout_ms = 1500;
    ccfg.reconnect_interval_ms = 300;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    /* Initially tries peer 0 which is unreachable. Drive recv to pump
     * reconnect logic and let failover timeout trigger a switch to peer 1. */
    bool connected = false;
    for (int i = 0; i < 20 && !connected; i++) {
        uint8_t dummy[256];
        size_t dummy_len;
        cli->ops->recv(cli, dummy, sizeof(dummy), &dummy_len, 200);

        uint8_t msg[] = "failover test";
        if (cli->ops->send(cli, msg, sizeof(msg)) == NX_OK) {
            uint8_t buf[256];
            size_t out_len = 0;
            if (srv->ops->recv(srv, buf, sizeof(buf), &out_len, 500) == NX_OK &&
                memcmp(buf, msg, sizeof(msg)) == 0) {
                connected = true;
            }
        }
    }

    ASSERT(connected, "failover reached second peer");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv->ops->destroy(srv);
    nx_platform_free(srv);
    PASS();
}

static void test_failover_stays_connected(void)
{
    TEST("failover stays with working peer (no parallel)");

    /* Two servers */
    nx_transport_t *srv1 = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg1;
    memset(&scfg1, 0, sizeof(scfg1));
    scfg1.listen_port = 19121;
    scfg1.listen_host = "127.0.0.1";
    ASSERT(srv1->ops->init(srv1, &scfg1) == NX_OK, "srv1 init");

    nx_transport_t *srv2 = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t scfg2;
    memset(&scfg2, 0, sizeof(scfg2));
    scfg2.listen_port = 19122;
    scfg2.listen_host = "127.0.0.1";
    ASSERT(srv2->ops->init(srv2, &scfg2) == NX_OK, "srv2 init");

    /* Client in failover mode: both peers available, should only use first */
    nx_transport_t *cli = nx_tcp_inet_transport_create();
    nx_tcp_inet_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.peers[0].host = "127.0.0.1";
    ccfg.peers[0].port = 19121;
    ccfg.peers[1].host = "127.0.0.1";
    ccfg.peers[1].port = 19122;
    ccfg.peer_count = 2;
    ccfg.failover = true;
    ccfg.failover_timeout_ms = 2000;
    ccfg.reconnect_interval_ms = 300;
    ASSERT(cli->ops->init(cli, &ccfg) == NX_OK, "cli init");

    usleep(200000);

    /* Send a few messages; srv1 should get them all, srv2 should get none */
    int srv1_count = 0, srv2_count = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t msg[] = "only first";
        ASSERT(cli->ops->send(cli, msg, sizeof(msg)) == NX_OK, "send");
        usleep(30000);

        uint8_t buf[256];
        size_t out_len = 0;
        if (srv1->ops->recv(srv1, buf, sizeof(buf), &out_len, 500) == NX_OK)
            srv1_count++;
    }
    /* srv2 should have nothing */
    {
        uint8_t buf[256];
        size_t out_len = 0;
        if (srv2->ops->recv(srv2, buf, sizeof(buf), &out_len, 200) == NX_OK)
            srv2_count++;
    }

    ASSERT(srv1_count == 3, "srv1 got all messages");
    ASSERT(srv2_count == 0, "srv2 got no messages");

    cli->ops->destroy(cli);
    nx_platform_free(cli);
    srv1->ops->destroy(srv1);
    nx_platform_free(srv1);
    srv2->ops->destroy(srv2);
    nx_platform_free(srv2);
    PASS();
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    (void)0;

    printf("TCP Internet Transport tests:\n");

    test_server_create();
    test_client_connect();
    test_send_recv();
    test_bidirectional();
    test_multi_client();
    test_reconnect();
    test_node_announce();
    test_hybrid_mode();
    test_packet_roundtrip();
    test_recv_timeout();
    test_psk_matching();
    test_psk_mismatch();
    test_allow_list_blocks();
    test_failover_connects_first();
    test_failover_stays_connected();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
