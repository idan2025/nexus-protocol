/*
 * NEXUS Protocol -- Adaptive Spreading Factor (ASF) Tests
 */

#include "nexus/lora_asf.h"
#include "nexus/platform.h"
#include <stdio.h>
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
static void test_asf_create_destroy(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);
    assert(asf != NULL);
    assert(nx_asf_get_current_sf(asf) == 10);
    assert(nx_asf_get_strategy(asf) == NX_ASF_STRATEGY_BALANCED);
    nx_asf_destroy(asf);
}

/* ── Test: create with clamped SF ─────────────────────────────────── */
static void test_asf_create_clamped(void)
{
    /* SF below 7 should be clamped to 7 */
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 3);
    assert(nx_asf_get_current_sf(asf) == 7);
    nx_asf_destroy(asf);

    /* SF above 12 should be clamped to 12 */
    asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 15);
    assert(nx_asf_get_current_sf(asf) == 12);
    nx_asf_destroy(asf);
}

/* ── Test: null safety ────────────────────────────────────────────── */
static void test_asf_null_safety(void)
{
    assert(nx_asf_get_current_sf(NULL) == 7);
    assert(nx_asf_get_strategy(NULL) == NX_ASF_STRATEGY_BALANCED);
    assert(nx_asf_get_recommended_sf(NULL) == 7);

    /* These should not crash */
    nx_asf_destroy(NULL);
    nx_asf_record_tx(NULL, 10);
    nx_asf_record_rx(NULL, -80, 5, 10);
    nx_asf_record_ack(NULL, true);
    nx_asf_set_bounds(NULL, 7, 12);
    nx_asf_set_strategy(NULL, NX_ASF_STRATEGY_AGGRESSIVE);
    nx_asf_set_target_airtime(NULL, 50);
    nx_asf_force_sf(NULL, 10);
    nx_asf_reset(NULL);
    nx_asf_get_stats(NULL, NULL, NULL, NULL, NULL);
    nx_asf_get_link_quality(NULL, NULL, NULL);
}

/* ── Test: strategy names ─────────────────────────────────────────── */
static void test_asf_strategy_names(void)
{
    assert(strcmp(nx_asf_strategy_name(NX_ASF_STRATEGY_CONSERVATIVE), "conservative") == 0);
    assert(strcmp(nx_asf_strategy_name(NX_ASF_STRATEGY_BALANCED), "balanced") == 0);
    assert(strcmp(nx_asf_strategy_name(NX_ASF_STRATEGY_AGGRESSIVE), "aggressive") == 0);
    assert(strcmp(nx_asf_strategy_name(NX_ASF_STRATEGY_ADAPTIVE), "adaptive") == 0);
    assert(strcmp(nx_asf_strategy_name(99), "unknown") == 0);
}

/* ── Test: set bounds ─────────────────────────────────────────────── */
static void test_asf_set_bounds(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    nx_asf_set_bounds(asf, 8, 11);
    assert(nx_asf_get_current_sf(asf) == 10);  /* still in range */

    /* Current SF outside new bounds gets clamped */
    nx_asf_set_bounds(asf, 11, 12);
    assert(nx_asf_get_current_sf(asf) == 11);

    /* min > max: min clamped to max */
    nx_asf_set_bounds(asf, 12, 8);
    assert(nx_asf_get_current_sf(asf) == 8);

    nx_asf_destroy(asf);
}

/* ── Test: force SF ───────────────────────────────────────────────── */
static void test_asf_force_sf(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);
    nx_asf_set_bounds(asf, 8, 11);

    nx_asf_force_sf(asf, 9);
    assert(nx_asf_get_current_sf(asf) == 9);

    /* Clamped to bounds */
    nx_asf_force_sf(asf, 7);
    assert(nx_asf_get_current_sf(asf) == 8);
    nx_asf_force_sf(asf, 12);
    assert(nx_asf_get_current_sf(asf) == 11);

    nx_asf_destroy(asf);
}

/* ── Test: record TX increments stats ─────────────────────────────── */
static void test_asf_record_tx(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    for (int i = 0; i < 5; i++) {
        nx_asf_record_tx(asf, 10);
    }

    uint32_t sent = 0, acked = 0, lost = 0;
    uint8_t rate = 0;
    nx_asf_get_stats(asf, &sent, &acked, &lost, &rate);
    assert(sent == 5);
    assert(acked == 0);
    assert(lost == 0);

    nx_asf_destroy(asf);
}

/* ── Test: record ACK tracks success/failure ──────────────────────── */
static void test_asf_record_ack(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    for (int i = 0; i < 10; i++) {
        nx_asf_record_tx(asf, 10);
        nx_asf_record_ack(asf, i < 7);  /* 7 success, 3 fail */
    }

    uint32_t sent, acked, lost;
    uint8_t rate;
    nx_asf_get_stats(asf, &sent, &acked, &lost, &rate);
    assert(sent == 10);
    assert(acked == 7);
    assert(lost == 3);

    nx_asf_destroy(asf);
}

/* ── Test: airtime estimation ─────────────────────────────────────── */
static void test_asf_airtime(void)
{
    uint32_t at7  = nx_asf_estimate_airtime(7, 100);
    uint32_t at10 = nx_asf_estimate_airtime(10, 100);
    uint32_t at12 = nx_asf_estimate_airtime(12, 100);

    /* Higher SF = much longer airtime */
    assert(at7 > 0);
    assert(at10 > at7);
    assert(at12 > at10);
    assert(at12 > at7 * 10);  /* SF12 should be ~15-30x SF7 */

    /* Zero-length payload still has base airtime */
    assert(nx_asf_estimate_airtime(10, 0) > 0);

    /* Out of range SF is clamped to SF7 */
    assert(nx_asf_estimate_airtime(5, 100) == at7);
}

/* ── Test: reset clears state ─────────────────────────────────────── */
static void test_asf_reset(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    for (int i = 0; i < 10; i++) {
        nx_asf_record_tx(asf, 10);
        nx_asf_record_ack(asf, true);
    }

    nx_asf_reset(asf);

    uint32_t sent, acked, lost;
    uint8_t rate;
    nx_asf_get_stats(asf, &sent, &acked, &lost, &rate);
    assert(sent == 0);
    assert(acked == 0);
    assert(lost == 0);
    assert(nx_asf_get_current_sf(asf) == 12);  /* reset goes to SF_MAX */

    nx_asf_destroy(asf);
}

/* ── Test: set strategy ───────────────────────────────────────────── */
static void test_asf_set_strategy(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    nx_asf_set_strategy(asf, NX_ASF_STRATEGY_AGGRESSIVE);
    assert(nx_asf_get_strategy(asf) == NX_ASF_STRATEGY_AGGRESSIVE);

    nx_asf_set_strategy(asf, NX_ASF_STRATEGY_CONSERVATIVE);
    assert(nx_asf_get_strategy(asf) == NX_ASF_STRATEGY_CONSERVATIVE);

    nx_asf_destroy(asf);
}

/* ── Test: all four strategies produce valid SF ───────────────────── */
static void test_asf_all_strategies(void)
{
    nx_asf_strategy_t strategies[] = {
        NX_ASF_STRATEGY_CONSERVATIVE,
        NX_ASF_STRATEGY_BALANCED,
        NX_ASF_STRATEGY_AGGRESSIVE,
        NX_ASF_STRATEGY_ADAPTIVE,
    };

    for (size_t s = 0; s < 4; s++) {
        nx_asf_state_t *asf = nx_asf_create(strategies[s], 10);

        /* Feed enough samples to trigger recommendation */
        for (int i = 0; i < 20; i++) {
            nx_asf_record_tx(asf, nx_asf_get_current_sf(asf));
            nx_asf_record_rx(asf, -80, 5, nx_asf_get_current_sf(asf));
            nx_asf_record_ack(asf, i % 4 != 0);
        }

        assert(nx_asf_get_recommended_sf(asf) >= 7);
        assert(nx_asf_get_recommended_sf(asf) <= 12);

        nx_asf_destroy(asf);
    }
}

/* ── Test: link quality getters ───────────────────────────────────── */
static void test_asf_link_quality(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);

    /* Feed some RX samples */
    for (int i = 0; i < 8; i++) {
        nx_asf_record_tx(asf, 10);
        nx_asf_record_rx(asf, -90, 3, 10);
    }

    int8_t rssi, snr;
    nx_asf_get_link_quality(asf, &rssi, &snr);
    /* Values should be populated (exact depends on averaging) */

    nx_asf_destroy(asf);
}

/* ── Test: target airtime clamp ───────────────────────────────────── */
static void test_asf_target_airtime(void)
{
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_ADAPTIVE, 10);

    nx_asf_set_target_airtime(asf, 50);
    /* 0 should clamp to 1 */
    nx_asf_set_target_airtime(asf, 0);
    /* 200 should clamp to 100 */
    nx_asf_set_target_airtime(asf, 200);

    nx_asf_destroy(asf);
}

int main(void)
{
    printf("test_asf\n");

    RUN_TEST(test_asf_create_destroy);
    RUN_TEST(test_asf_create_clamped);
    RUN_TEST(test_asf_null_safety);
    RUN_TEST(test_asf_strategy_names);
    RUN_TEST(test_asf_set_bounds);
    RUN_TEST(test_asf_force_sf);
    RUN_TEST(test_asf_record_tx);
    RUN_TEST(test_asf_record_ack);
    RUN_TEST(test_asf_airtime);
    RUN_TEST(test_asf_reset);
    RUN_TEST(test_asf_set_strategy);
    RUN_TEST(test_asf_all_strategies);
    RUN_TEST(test_asf_link_quality);
    RUN_TEST(test_asf_target_airtime);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
