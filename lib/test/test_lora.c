/*
 * NEXUS Protocol -- LoRa Transport Unit Tests (Mock Radio)
 */
#include "nexus/lora_radio.h"
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
    TEST("mock radio create and destroy");

    nx_lora_radio_t *r = nx_lora_mock_create();
    ASSERT(r != NULL, "create");
    ASSERT(r->ops != NULL, "ops");
    ASSERT(r->state == NX_RADIO_SLEEP, "initial state");

    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    ASSERT(r->ops->init(r, &cfg) == NX_OK, "init");
    ASSERT(r->state == NX_RADIO_STANDBY, "standby after init");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_loopback(void)
{
    TEST("mock radio linked loopback");

    nx_lora_radio_t *a = nx_lora_mock_create();
    nx_lora_radio_t *b = nx_lora_mock_create();
    ASSERT(a && b, "create");

    nx_lora_mock_link(a, b);

    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    a->ops->init(a, &cfg);
    b->ops->init(b, &cfg);

    /* Transmit from A, receive on B */
    const char *msg = "Hello LoRa";
    ASSERT(a->ops->transmit(a, (const uint8_t *)msg, strlen(msg)) == NX_OK,
           "transmit");

    uint8_t buf[256];
    size_t out_len = 0;
    nx_lora_rx_info_t rx_info;
    ASSERT(b->ops->receive(b, buf, sizeof(buf), &out_len, &rx_info, 100)
           == NX_OK, "receive");
    ASSERT(out_len == strlen(msg), "length");
    ASSERT(memcmp(buf, msg, out_len) == 0, "data");
    ASSERT(rx_info.rssi < 0, "rssi negative");

    /* And the other direction */
    const char *reply = "Mesh reply";
    ASSERT(b->ops->transmit(b, (const uint8_t *)reply, strlen(reply)) == NX_OK,
           "transmit back");
    ASSERT(a->ops->receive(a, buf, sizeof(buf), &out_len, &rx_info, 100)
           == NX_OK, "receive back");
    ASSERT(memcmp(buf, reply, out_len) == 0, "reply data");

    a->ops->destroy(a);
    b->ops->destroy(b);
    nx_platform_free(a);
    nx_platform_free(b);
    PASS();
}

static void test_mock_timeout(void)
{
    TEST("mock radio receive timeout");

    nx_lora_radio_t *r = nx_lora_mock_create();
    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);

    /* No linked peer, no data -- should timeout */
    uint8_t buf[64];
    size_t out_len;
    nx_lora_rx_info_t rx_info;
    nx_err_t err = r->ops->receive(r, buf, sizeof(buf), &out_len, &rx_info, 10);
    ASSERT(err == NX_ERR_TIMEOUT, "should timeout");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_unlinked_transmit(void)
{
    TEST("unlinked mock transmit fails");

    nx_lora_radio_t *r = nx_lora_mock_create();
    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);

    nx_err_t err = r->ops->transmit(r, (const uint8_t *)"test", 4);
    ASSERT(err == NX_ERR_TRANSPORT, "should fail");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_cad(void)
{
    TEST("mock CAD always returns clear");

    nx_lora_radio_t *r = nx_lora_mock_create();
    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);

    bool activity = true;
    ASSERT(r->ops->cad(r, &activity) == NX_OK, "cad");
    ASSERT(activity == false, "channel should be clear");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

static void test_mock_sleep_standby(void)
{
    TEST("mock sleep/standby state transitions");

    nx_lora_radio_t *r = nx_lora_mock_create();
    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    r->ops->init(r, &cfg);
    ASSERT(r->state == NX_RADIO_STANDBY, "standby");

    r->ops->sleep(r);
    ASSERT(r->state == NX_RADIO_SLEEP, "sleep");

    r->ops->standby(r);
    ASSERT(r->state == NX_RADIO_STANDBY, "back to standby");

    r->ops->destroy(r);
    nx_platform_free(r);
    PASS();
}

/* ── LoRa Transport Tests ────────────────────────────────────────────── */

static void test_lora_transport_loopback(void)
{
    TEST("LoRa transport send/recv via mock radio");

    nx_lora_radio_t *ra = nx_lora_mock_create();
    nx_lora_radio_t *rb = nx_lora_mock_create();
    nx_lora_mock_link(ra, rb);

    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    ra->ops->init(ra, &cfg);
    rb->ops->init(rb, &cfg);

    /* Create transports */
    nx_transport_t *ta = nx_lora_transport_create();
    nx_transport_t *tb = nx_lora_transport_create();
    ASSERT(ta && tb, "create transports");

    /* Init with radio pointers */
    nx_lora_radio_t *ra_ptr = ra;
    nx_lora_radio_t *rb_ptr = rb;
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

/* ── Airtime Calculation Tests ───────────────────────────────────────── */

static void test_airtime_calculation(void)
{
    TEST("airtime calculation produces reasonable values");

    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;

    /* SF9, BW250kHz, 50 byte payload */
    uint32_t t50 = nx_lora_airtime_ms(&cfg, 50);
    ASSERT(t50 > 0, "non-zero");
    ASSERT(t50 < 1000, "under 1 second for 50B at SF9/BW250");

    /* Larger payload should take longer */
    uint32_t t200 = nx_lora_airtime_ms(&cfg, 200);
    ASSERT(t200 > t50, "larger payload takes longer");

    /* Higher SF should take longer */
    nx_lora_config_t cfg12 = cfg;
    cfg12.spreading_factor = 12;
    cfg12.bandwidth_hz = 125000;
    uint32_t t50_sf12 = nx_lora_airtime_ms(&cfg12, 50);
    ASSERT(t50_sf12 > t50, "SF12 slower than SF9");

    /* Empty payload */
    uint32_t t0 = nx_lora_airtime_ms(&cfg, 0);
    ASSERT(t0 > 0, "preamble alone has airtime");
    ASSERT(t0 < t50, "empty shorter than 50B");

    PASS();
}

static void test_airtime_null_config(void)
{
    TEST("airtime returns 0 for null/invalid config");

    ASSERT(nx_lora_airtime_ms(NULL, 50) == 0, "null config");

    nx_lora_config_t bad = NX_LORA_CONFIG_DEFAULT;
    bad.bandwidth_hz = 0;
    ASSERT(nx_lora_airtime_ms(&bad, 50) == 0, "zero bandwidth");

    PASS();
}

static void test_mock_multiple_packets(void)
{
    TEST("mock radio handles multiple queued packets");

    nx_lora_radio_t *a = nx_lora_mock_create();
    nx_lora_radio_t *b = nx_lora_mock_create();
    nx_lora_mock_link(a, b);

    nx_lora_config_t cfg = NX_LORA_CONFIG_DEFAULT;
    a->ops->init(a, &cfg);
    b->ops->init(b, &cfg);

    /* Send 5 packets */
    for (int i = 0; i < 5; i++) {
        uint8_t data = (uint8_t)i;
        ASSERT(a->ops->transmit(a, &data, 1) == NX_OK, "tx");
    }

    /* Receive all 5 in order */
    for (int i = 0; i < 5; i++) {
        uint8_t buf[8];
        size_t len;
        nx_lora_rx_info_t info;
        ASSERT(b->ops->receive(b, buf, sizeof(buf), &len, &info, 100)
               == NX_OK, "rx");
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
    printf("=== NEXUS LoRa Tests ===\n");

    test_mock_create();
    test_mock_loopback();
    test_mock_timeout();
    test_mock_unlinked_transmit();
    test_mock_cad();
    test_mock_sleep_standby();
    test_lora_transport_loopback();
    test_airtime_calculation();
    test_airtime_null_config();
    test_mock_multiple_packets();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
