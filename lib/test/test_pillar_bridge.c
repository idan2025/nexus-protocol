/*
 * NEXUS Protocol -- Pillar hub-bridging regression test
 *
 * Verifies the fix for the "two phones connected to one pillar can only see
 * the pillar, not each other" bug.
 *
 * Topology:
 *
 *     phone_a  ----TCP----+
 *                          \
 *                           +----> pillar (single tcp_inet transport listening on a port)
 *                          /
 *     phone_b  ----TCP----+
 *
 * Before the fix, the pillar's node layer skipped its ingress transport when
 * forwarding flooded packets, so a packet from phone_a never reached phone_b
 * (both attached to the same transport instance).
 *
 * After the fix, the pillar's tcp_inet transport exposes send_bridge() which
 * the node layer calls to re-distribute the packet to every connected peer
 * EXCEPT the one it just arrived on.
 *
 * This test exercises the transport+send_bridge path directly (without
 * spinning up full nodes) so we can assert: (a) bridge delivery happens,
 * (b) the ingress peer does NOT see the echo.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nexus/transport.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-55s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Helpers ─────────────────────────────────────────────────────────── */

static nx_transport_t *make_pillar(uint16_t port)
{
    nx_transport_t *t = nx_tcp_inet_transport_create();
    if (!t) return NULL;
    nx_tcp_inet_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.listen_host = "127.0.0.1";
    cfg.listen_port = port;
    if (t->ops->init(t, &cfg) != NX_OK) {
        nx_platform_free(t);
        return NULL;
    }
    return t;
}

static nx_transport_t *make_phone(uint16_t pillar_port)
{
    nx_transport_t *t = nx_tcp_inet_transport_create();
    if (!t) return NULL;
    nx_tcp_inet_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.peers[0].host = "127.0.0.1";
    cfg.peers[0].port = pillar_port;
    cfg.peer_count = 1;
    if (t->ops->init(t, &cfg) != NX_OK) {
        nx_platform_free(t);
        return NULL;
    }
    return t;
}

static void destroy(nx_transport_t *t)
{
    if (!t) return;
    t->ops->destroy(t);
    nx_platform_free(t);
}

/* Drain a single packet (or timeout). */
static nx_err_t drain_one(nx_transport_t *t, uint8_t *buf, size_t cap,
                          size_t *out_len, uint32_t timeout_ms)
{
    return t->ops->recv(t, buf, cap, out_len, timeout_ms);
}

/* Pump pillar recv briefly so accept_pending() runs and connections appear. */
static void pump_pillar(nx_transport_t *pillar, uint32_t ms)
{
    uint8_t junk[256];
    size_t  n = 0;
    (void)pillar->ops->recv(pillar, junk, sizeof(junk), &n, ms);
}

/* ── Test 1: send_bridge op is exported by tcp_inet ──────────────────── */

static void test_send_bridge_exported(void)
{
    TEST("tcp_inet transport exports send_bridge op");

    nx_transport_t *t = nx_tcp_inet_transport_create();
    ASSERT(t != NULL, "alloc");
    ASSERT(t->ops != NULL, "ops");
    ASSERT(t->ops->send_bridge != NULL, "send_bridge present");

    nx_tcp_inet_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.listen_host = "127.0.0.1";
    cfg.listen_port = 19200;
    ASSERT(t->ops->init(t, &cfg) == NX_OK, "init");

    destroy(t);
    PASS();
}

/* ── Test 2: send_bridge with no prior recv broadcasts to all peers ──── */

static void test_send_bridge_no_ingress(void)
{
    TEST("send_bridge with no recv yet broadcasts to all peers");

    nx_transport_t *pillar = make_pillar(19201);
    ASSERT(pillar != NULL, "pillar");

    nx_transport_t *a = make_phone(19201);
    nx_transport_t *b = make_phone(19201);
    ASSERT(a && b, "phones");

    /* Let the pillar accept both connections. */
    pump_pillar(pillar, 200);
    pump_pillar(pillar, 50);

    uint8_t msg[] = "hello-broadcast";
    /* last_rx_conn_idx is -1, so send_bridge should hit both peers. */
    nx_err_t err = nx_transport_send_bridge(pillar, msg, sizeof(msg));
    ASSERT(err == NX_OK, "send_bridge");

    uint8_t buf_a[64], buf_b[64];
    size_t la = 0, lb = 0;
    ASSERT(drain_one(a, buf_a, sizeof(buf_a), &la, 1500) == NX_OK, "a recv");
    ASSERT(drain_one(b, buf_b, sizeof(buf_b), &lb, 1500) == NX_OK, "b recv");
    ASSERT(la == sizeof(msg) && memcmp(buf_a, msg, la) == 0, "a payload");
    ASSERT(lb == sizeof(msg) && memcmp(buf_b, msg, lb) == 0, "b payload");

    destroy(a); destroy(b); destroy(pillar);
    PASS();
}

/* ── Test 3: send_bridge skips ingress (THE bug fix) ─────────────────── */

static void test_send_bridge_skips_ingress(void)
{
    TEST("pillar bridges A -> B without echoing back to A");

    nx_transport_t *pillar = make_pillar(19202);
    ASSERT(pillar != NULL, "pillar");

    nx_transport_t *a = make_phone(19202);
    nx_transport_t *b = make_phone(19202);
    ASSERT(a && b, "phones");

    pump_pillar(pillar, 200);
    pump_pillar(pillar, 50);

    /* Phone A sends a message TO the pillar. */
    uint8_t msg[] = "from-A-to-everyone";
    ASSERT(a->ops->send(a, msg, sizeof(msg)) == NX_OK, "a send");

    /* Pillar receives it. This sets last_rx_conn_idx to A's conn slot. */
    uint8_t buf[64];
    size_t  n = 0;
    ASSERT(drain_one(pillar, buf, sizeof(buf), &n, 2000) == NX_OK, "pillar recv");
    ASSERT(n == sizeof(msg) && memcmp(buf, msg, n) == 0, "pillar payload");

    /* Pillar bridges. send_bridge MUST forward to B but NOT echo back to A. */
    ASSERT(nx_transport_send_bridge(pillar, buf, n) == NX_OK, "bridge");

    /* B should receive it. */
    uint8_t buf_b[64];
    size_t  lb = 0;
    ASSERT(drain_one(b, buf_b, sizeof(buf_b), &lb, 1500) == NX_OK, "b recv");
    ASSERT(lb == sizeof(msg) && memcmp(buf_b, msg, lb) == 0, "b payload");

    /* A should NOT receive it back. Short timeout = ingress was skipped. */
    uint8_t buf_a[64];
    size_t  la = 0;
    nx_err_t err = drain_one(a, buf_a, sizeof(buf_a), &la, 300);
    ASSERT(err == NX_ERR_TIMEOUT, "a must NOT receive echo");

    destroy(a); destroy(b); destroy(pillar);
    PASS();
}

/* ── Test 4: ingress index updates after each recv ───────────────────── */

static void test_send_bridge_alternating_ingress(void)
{
    TEST("ingress index updates per recv (A then B)");

    nx_transport_t *pillar = make_pillar(19203);
    nx_transport_t *a = make_phone(19203);
    nx_transport_t *b = make_phone(19203);
    ASSERT(pillar && a && b, "init");

    pump_pillar(pillar, 200);
    pump_pillar(pillar, 50);

    uint8_t mA[] = "msg-A";
    uint8_t mB[] = "msg-B";
    uint8_t buf[64];
    size_t  n = 0;

    /* A -> pillar; bridge should reach B only. */
    ASSERT(a->ops->send(a, mA, sizeof(mA)) == NX_OK, "a send");
    ASSERT(drain_one(pillar, buf, sizeof(buf), &n, 2000) == NX_OK, "p recv A");
    ASSERT(nx_transport_send_bridge(pillar, buf, n) == NX_OK, "bridge A");

    uint8_t bb[64]; size_t lb = 0;
    ASSERT(drain_one(b, bb, sizeof(bb), &lb, 1500) == NX_OK, "b got A");
    ASSERT(memcmp(bb, mA, lb) == 0, "b payload A");

    uint8_t aa[64]; size_t la = 0;
    ASSERT(drain_one(a, aa, sizeof(aa), &la, 300) == NX_ERR_TIMEOUT,
           "a no echo after A");

    /* Now B -> pillar; bridge should reach A only. */
    ASSERT(b->ops->send(b, mB, sizeof(mB)) == NX_OK, "b send");
    n = 0;
    ASSERT(drain_one(pillar, buf, sizeof(buf), &n, 2000) == NX_OK, "p recv B");
    ASSERT(nx_transport_send_bridge(pillar, buf, n) == NX_OK, "bridge B");

    la = 0;
    ASSERT(drain_one(a, aa, sizeof(aa), &la, 1500) == NX_OK, "a got B");
    ASSERT(memcmp(aa, mB, la) == 0, "a payload B");

    lb = 0;
    ASSERT(drain_one(b, bb, sizeof(bb), &lb, 300) == NX_ERR_TIMEOUT,
           "b no echo after B");

    destroy(a); destroy(b); destroy(pillar);
    PASS();
}

/* ── Test 5: send_bridge on single-peer transport returns INVALID_ARG ── */

static void test_send_bridge_unsupported(void)
{
    TEST("non-hub transport rejects send_bridge");

    /* The pipe transport is single-peer and does not implement send_bridge.
     * nx_transport_send_bridge must refuse cleanly. (Pipe owns its own
     * struct via ops->destroy, hence the direct call below.) */
    nx_transport_t *p = nx_pipe_transport_create();
    ASSERT(p != NULL, "alloc");
    ASSERT(p->ops->init(p, NULL) == NX_OK, "init");
    ASSERT(p->ops->send_bridge == NULL, "no send_bridge");

    p->active = true;
    uint8_t msg[] = "x";
    nx_err_t err = nx_transport_send_bridge(p, msg, sizeof(msg));
    ASSERT(err == NX_ERR_INVALID_ARG, "must reject");

    p->ops->destroy(p);
    PASS();
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("Pillar bridging tests\n");
    printf("=====================\n");

    test_send_bridge_exported();
    test_send_bridge_no_ingress();
    test_send_bridge_skips_ingress();
    test_send_bridge_alternating_ingress();
    test_send_bridge_unsupported();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
