/*
 * NEXUS Protocol -- WiFi HaLow Transport Tests
 */

#include "nexus/halow.h"
#include "nexus/transport.h"
#include "nexus/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    printf("  %-45s ", #fn); \
    fn(); \
    tests_run++; tests_passed++; \
    printf("PASS\n"); \
} while(0)

/* ── Test: create/destroy ─────────────────────────────────────────── */
static void test_halow_create_destroy(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    assert(t != NULL);
    assert(t->type == NX_TRANSPORT_WIFI);
    assert(t->ops != NULL);
    assert(strcmp(t->name, "halow") == 0);
    assert(t->active == false);

    nx_transport_destroy(t);
}

/* ── Test: init with default config ───────────────────────────────── */
static void test_halow_init_default(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    assert(t != NULL);

    /* init with NULL config uses defaults */
    nx_err_t err = t->ops->init(t, NULL);
    assert(err == NX_OK);
    assert(t->active == true);
    assert(t->state != NULL);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: init with custom config ────────────────────────────────── */
static void test_halow_init_custom(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    assert(t != NULL);

    nx_halow_config_t cfg = {
        .channel = 11,
        .bandwidth = 2,
        .rate = 2,        /* 600K */
        .tx_power_dbm = 14,
        .ssid = {'T', 'E', 'S', 'T'},
        .ssid_len = 4,
        .password = {0},
        .ap_mode = false,
        .ps_mode = NX_HALOW_PS_WMM,
        .security = NX_HALOW_SEC_OPEN,
        .mesh_enabled = true,
        .mesh_id = {'M', 'E', 'S', 'H'},
        .ifname = {'w', 'l', 'a', 'n', '0'},
    };

    nx_err_t err = t->ops->init(t, &cfg);
    assert(err == NX_OK);
    assert(t->active == true);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: init null transport ────────────────────────────────────── */
static void test_halow_init_null(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    assert(t != NULL);

    /* init with NULL transport pointer should fail */
    nx_err_t err = t->ops->init(NULL, NULL);
    assert(err == NX_ERR_INVALID_ARG);

    free(t);
}

/* ── Test: send without init ──────────────────────────────────────── */
static void test_halow_send_no_init(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    assert(t != NULL);

    uint8_t data[] = {1, 2, 3};
    nx_err_t err = t->ops->send(t, data, sizeof(data));
    /* state is NULL, should return INVALID_ARG */
    assert(err == NX_ERR_INVALID_ARG);

    free(t);
}

/* ── Test: send null args ─────────────────────────────────────────── */
static void test_halow_send_null_args(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    nx_err_t err = t->ops->send(t, NULL, 10);
    assert(err == NX_ERR_INVALID_ARG);

    err = t->ops->send(NULL, (uint8_t[]){1}, 1);
    assert(err == NX_ERR_INVALID_ARG);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: send oversized ─────────────────────────────────────────── */
static void test_halow_send_oversized(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    uint8_t big[2048];
    memset(big, 0xAA, sizeof(big));
    nx_err_t err = t->ops->send(t, big, sizeof(big));
    assert(err == NX_ERR_BUFFER_TOO_SMALL);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: recv null args ─────────────────────────────────────────── */
static void test_halow_recv_null_args(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    uint8_t buf[256];
    size_t out_len;

    nx_err_t err = t->ops->recv(NULL, buf, sizeof(buf), &out_len, 0);
    assert(err == NX_ERR_INVALID_ARG);

    err = t->ops->recv(t, NULL, sizeof(buf), &out_len, 0);
    assert(err == NX_ERR_INVALID_ARG);

    err = t->ops->recv(t, buf, sizeof(buf), NULL, 0);
    assert(err == NX_ERR_INVALID_ARG);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: recv timeout ───────────────────────────────────────────── */
static void test_halow_recv_timeout(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    uint8_t buf[256];
    size_t out_len = 0;

    uint64_t before = nx_platform_time_ms();
    nx_err_t err = t->ops->recv(t, buf, sizeof(buf), &out_len, 50);
    uint64_t elapsed = nx_platform_time_ms() - before;

    assert(err == NX_ERR_TIMEOUT);
    assert(elapsed >= 40);  /* allow some slack */

    t->ops->destroy(t);
    free(t);
}

/* ── Test: metrics ────────────────────────────────────────────────── */
static void test_halow_metrics(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    int8_t rssi;
    uint8_t snr, quality;
    nx_err_t err = nx_halow_get_metrics(t, &rssi, &snr, &quality);
    assert(err == NX_OK);
    assert(quality == 128);  /* default link quality */

    t->ops->destroy(t);
    free(t);
}

/* ── Test: metrics null transport ─────────────────────────────────── */
static void test_halow_metrics_null(void)
{
    nx_err_t err = nx_halow_get_metrics(NULL, NULL, NULL, NULL);
    assert(err == NX_ERR_INVALID_ARG);
}

/* ── Test: statistics ─────────────────────────────────────────────── */
static void test_halow_stats(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    /* Send a few packets */
    uint8_t data[64];
    memset(data, 0x55, sizeof(data));
    for (int i = 0; i < 5; i++) {
        t->ops->send(t, data, sizeof(data));
    }

    uint64_t tx_pkt, tx_bytes, rx_pkt, rx_bytes, tx_err, rx_err;
    nx_err_t err = nx_halow_get_stats(t, &tx_pkt, &tx_bytes,
                                       &rx_pkt, &rx_bytes,
                                       &tx_err, &rx_err);
    assert(err == NX_OK);
    assert(tx_pkt == 5);
    assert(tx_bytes == 5 * 64);
    assert(rx_pkt == 0);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: reconfigure ────────────────────────────────────────────── */
static void test_halow_reconfigure(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    nx_err_t err = nx_halow_reconfigure(t, 11, 2, 2);
    assert(err == NX_OK);

    /* Invalid channel */
    err = nx_halow_reconfigure(t, 0, 1, 0);
    assert(err == NX_ERR_INVALID_ARG);
    err = nx_halow_reconfigure(t, 15, 1, 0);
    assert(err == NX_ERR_INVALID_ARG);

    /* Invalid bandwidth */
    err = nx_halow_reconfigure(t, 6, 3, 0);
    assert(err == NX_ERR_INVALID_ARG);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: airtime estimation ─────────────────────────────────────── */
static void test_halow_airtime(void)
{
    uint32_t airtime_150k = nx_halow_estimate_airtime(100, 0);
    uint32_t airtime_300k = nx_halow_estimate_airtime(100, 1);
    uint32_t airtime_600k = nx_halow_estimate_airtime(100, 2);

    /* Faster rates should have lower airtime */
    assert(airtime_150k > airtime_300k);
    assert(airtime_300k > airtime_600k);
    assert(airtime_600k > 0);

    /* Invalid rate */
    uint32_t bad = nx_halow_estimate_airtime(100, 99);
    assert(bad == 0);
}

/* ── Test: channel assessment ─────────────────────────────────────── */
static void test_halow_assess_channel(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    t->ops->init(t, NULL);

    uint8_t rate, power;
    assert(nx_halow_assess_channel(t, &rate, &power) == NX_OK);
    /* With default snr=0, should pick lowest rate, highest power */
    assert(rate == 0);   /* 150K */
    assert(power == 20);

    t->ops->destroy(t);
    free(t);
}

/* ── Test: destroy is safe on NULL ────────────────────────────────── */
static void test_halow_destroy_safe(void)
{
    nx_transport_t *t = nx_halow_transport_create();
    /* destroy without init - state is NULL, should not crash */
    t->ops->destroy(t);
    free(t);
}

int main(void)
{
    printf("test_halow\n");

    RUN_TEST(test_halow_create_destroy);
    RUN_TEST(test_halow_init_default);
    RUN_TEST(test_halow_init_custom);
    RUN_TEST(test_halow_init_null);
    RUN_TEST(test_halow_send_no_init);
    RUN_TEST(test_halow_send_null_args);
    RUN_TEST(test_halow_send_oversized);
    RUN_TEST(test_halow_recv_null_args);
    RUN_TEST(test_halow_recv_timeout);
    RUN_TEST(test_halow_metrics);
    RUN_TEST(test_halow_metrics_null);
    RUN_TEST(test_halow_stats);
    RUN_TEST(test_halow_reconfigure);
    RUN_TEST(test_halow_airtime);
    RUN_TEST(test_halow_assess_channel);
    RUN_TEST(test_halow_destroy_safe);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
