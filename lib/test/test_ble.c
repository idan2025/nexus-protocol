/*
 * NEXUS Protocol -- BLE Transport Unit Tests (Mock Radio)
 */
#include "nexus/ble_radio.h"
#include "nexus/transport.h"
#include "nexus/packet.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

/* ── Mock Radio Tests ────────────────────────────────────────────────── */

static void test_mock_create(void)
{
    TEST("BLE mock radio create and destroy");

    nx_ble_radio_t *r = nx_ble_mock_create();
    ASSERT(r != NULL, "create");
    ASSERT(r->ops != NULL, "ops");
    ASSERT(r->state == NX_BLE_IDLE, "initial state");

    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    ASSERT(r->ops->init(r, &cfg) == NX_OK, "init");
    ASSERT(r->state == NX_BLE_ADVERTISING, "advertising after init");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_loopback(void)
{
    TEST("BLE mock radio linked loopback");

    nx_ble_radio_t *a = nx_ble_mock_create();
    nx_ble_radio_t *b = nx_ble_mock_create();
    ASSERT(a && b, "create");

    nx_ble_mock_link(a, b);

    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    a->ops->init(a, &cfg);
    b->ops->init(b, &cfg);

    /* Transmit from A, receive on B */
    const char *msg = "Hello BLE";
    ASSERT(a->ops->send(a, (const uint8_t *)msg, strlen(msg)) == NX_OK,
           "send");

    uint8_t buf[256];
    size_t out_len = 0;
    ASSERT(b->ops->recv(b, buf, sizeof(buf), &out_len, 100) == NX_OK,
           "recv");
    ASSERT(out_len == strlen(msg), "length");
    ASSERT(memcmp(buf, msg, out_len) == 0, "data");

    /* And the other direction */
    const char *reply = "BLE reply";
    ASSERT(b->ops->send(b, (const uint8_t *)reply, strlen(reply)) == NX_OK,
           "send back");
    ASSERT(a->ops->recv(a, buf, sizeof(buf), &out_len, 100) == NX_OK,
           "recv back");
    ASSERT(memcmp(buf, reply, out_len) == 0, "reply data");

    a->ops->destroy(a);
    b->ops->destroy(b);
    nx_platform_free(a);
    nx_platform_free(b);
    PASS();
}

static void test_mock_timeout(void)
{
    TEST("BLE mock radio receive timeout");

    nx_ble_radio_t *r = nx_ble_mock_create();
    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);

    uint8_t buf[64];
    size_t out_len;
    nx_err_t err = r->ops->recv(r, buf, sizeof(buf), &out_len, 10);
    ASSERT(err == NX_ERR_TIMEOUT, "should timeout");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_unlinked(void)
{
    TEST("BLE unlinked mock send fails");

    nx_ble_radio_t *r = nx_ble_mock_create();
    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);

    nx_err_t err = r->ops->send(r, (const uint8_t *)"test", 4);
    ASSERT(err == NX_ERR_TRANSPORT, "should fail");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_ble_transport_loopback(void)
{
    TEST("BLE transport send/recv via mock radio");

    nx_ble_radio_t *ra = nx_ble_mock_create();
    nx_ble_radio_t *rb = nx_ble_mock_create();
    nx_ble_mock_link(ra, rb);

    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    ra->ops->init(ra, &cfg);
    rb->ops->init(rb, &cfg);

    /* Create transports */
    nx_transport_t *ta = nx_ble_transport_create();
    nx_transport_t *tb = nx_ble_transport_create();
    ASSERT(ta && tb, "create transports");

    /* Init with radio pointers */
    nx_ble_radio_t *ra_ptr = ra;
    nx_ble_radio_t *rb_ptr = rb;
    ASSERT(ta->ops->init(ta, &ra_ptr) == NX_OK, "init ta");
    ASSERT(tb->ops->init(tb, &rb_ptr) == NX_OK, "init tb");
    ASSERT(ta->active, "ta active");
    ASSERT(tb->active, "tb active");

    /* Build and send a packet via transport A */
    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    pkt.header.ttl = 7;
    pkt.header.dst = (nx_addr_short_t){{0xDE, 0xAD, 0xBE, 0xEF}};
    pkt.header.src = (nx_addr_short_t){{0xCA, 0xFE, 0xBA, 0xBE}};
    pkt.header.seq_id = 1;
    pkt.header.payload_len = 5;
    memcpy(pkt.payload, "hello", 5);

    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(n > 0, "serialize");

    ASSERT(nx_transport_send(ta, wire, (size_t)n) == NX_OK, "send");

    /* Receive on transport B */
    uint8_t recv_buf[NX_MAX_PACKET];
    size_t recv_len = 0;
    ASSERT(nx_transport_recv(tb, recv_buf, sizeof(recv_buf), &recv_len, 100)
           == NX_OK, "recv");
    ASSERT(recv_len == (size_t)n, "recv len");

    /* Deserialize and verify */
    nx_packet_t out;
    ASSERT(nx_packet_deserialize(recv_buf, recv_len, &out) == NX_OK, "deser");
    ASSERT(memcmp(out.payload, "hello", 5) == 0, "payload");

    ta->ops->destroy(ta);
    tb->ops->destroy(tb);
    nx_platform_free(ta);
    nx_platform_free(tb);
    ra->ops->destroy(ra);
    rb->ops->destroy(rb);
    nx_platform_free(ra);
    nx_platform_free(rb);
    PASS();
}

static void test_mock_multiple_packets(void)
{
    TEST("BLE mock radio handles multiple queued packets");

    nx_ble_radio_t *a = nx_ble_mock_create();
    nx_ble_radio_t *b = nx_ble_mock_create();
    nx_ble_mock_link(a, b);

    nx_ble_config_t cfg = NX_BLE_CONFIG_DEFAULT;
    a->ops->init(a, &cfg);
    b->ops->init(b, &cfg);

    /* Send 5 packets */
    for (int i = 0; i < 5; i++) {
        uint8_t data = (uint8_t)i;
        ASSERT(a->ops->send(a, &data, 1) == NX_OK, "tx");
    }

    /* Receive all 5 in FIFO order */
    for (int i = 0; i < 5; i++) {
        uint8_t buf[8];
        size_t len;
        ASSERT(b->ops->recv(b, buf, sizeof(buf), &len, 100) == NX_OK, "rx");
        ASSERT(len == 1, "len");
        ASSERT(buf[0] == (uint8_t)i, "order");
    }

    a->ops->destroy(a);
    b->ops->destroy(b);
    nx_platform_free(a);
    nx_platform_free(b);
    PASS();
}

int main(void)
{
    printf("=== NEXUS BLE Tests ===\n");

    test_mock_create();
    test_mock_loopback();
    test_mock_timeout();
    test_mock_unlinked();
    test_ble_transport_loopback();
    test_mock_multiple_packets();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
