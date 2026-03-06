/*
 * NEXUS Protocol -- Stress Tests
 */

#define _POSIX_C_SOURCE 200809L

#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/platform.h"
#include "nexus/lora_asf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* Test configuration */
#define STRESS_NUM_NODES        10
#define STRESS_TEST_DURATION_MS 30000
#define STRESS_LARGE_MSG_SIZE   2048

/* Statistics */
typedef struct {
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    double   max_latency_ms;
} stress_stats_t;

/* Test node context */
typedef struct {
    nx_node_t           node;
    nx_identity_t       identity;
    stress_stats_t      stats;
} test_node_t;

static test_node_t g_nodes[STRESS_NUM_NODES];
static uint64_t g_start_time;

/* Callback for received data */
static void on_data_cb(const nx_addr_short_t *src,
                       const uint8_t *data, size_t len, void *user)
{
    test_node_t *node = (test_node_t *)user;
    uint64_t now = nx_platform_time_ms();
    
    (void)src;
    (void)data;
    
    node->stats.packets_received++;
    node->stats.bytes_received += len;
    
    double latency = (double)(now - g_start_time);
    if (latency > node->stats.max_latency_ms) {
        node->stats.max_latency_ms = latency;
    }
}

/* Generate random payload */
static void generate_payload(uint8_t *buf, size_t len, uint32_t seq)
{
    srand(seq);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() % 256);
    }
}

/* Sleep helper */
static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Test 1: High volume single-hop flooding */
static void test_high_volume_flood(void)
{
    printf("\n=== Test 1: High Volume Flood ===\n");
    
    uint8_t payload[NX_MAX_PAYLOAD];
    uint32_t seq = 0;
    uint64_t start = nx_platform_time_ms();
    uint64_t sent = 0;
    
    test_node_t *sender = &g_nodes[0];
    
    while (nx_platform_time_ms() - start < 5000) {
        generate_payload(payload, sizeof(payload), seq++);
        
        nx_err_t err = nx_node_send(&sender->node,
                                     &NX_ADDR_BROADCAST_SHORT,
                                     payload, sizeof(payload));
        
        if (err == NX_OK) {
            sent++;
            sender->stats.packets_sent++;
            sender->stats.bytes_sent += sizeof(payload);
        }
        
        if (sent % 100 == 0) {
            sleep_ms(1);
        }
    }
    
    uint64_t elapsed = nx_platform_time_ms() - start;
    double rate = (double)sent / (elapsed / 1000.0);
    
    printf("  Packets sent: %lu\n", (unsigned long)sent);
    printf("  Rate: %.1f packets/sec\n", rate);
    printf("  Throughput: %.1f KB/sec\n", rate * NX_MAX_PAYLOAD / 1024.0);
}

/* Test 2: Multi-hop routing stress */
static void test_multihop_routing(void)
{
    printf("\n=== Test 2: Multi-hop Routing ===\n");
    
    uint8_t payload[64];
    uint32_t seq = 0;
    uint64_t start = nx_platform_time_ms();
    uint64_t sent = 0;
    
    while (nx_platform_time_ms() - start < 5000) {
        generate_payload(payload, sizeof(payload), seq++);
        
        nx_err_t err = nx_node_send(&g_nodes[0].node,
                                     &g_nodes[4].identity.short_addr,
                                     payload, sizeof(payload));
        
        if (err == NX_OK) {
            sent++;
            g_nodes[0].stats.packets_sent++;
        }
        
        for (int i = 0; i < 5; i++) {
            nx_node_poll(&g_nodes[i].node, 0);
        }
    }
    
    printf("  Sent: %lu packets\n", (unsigned long)sent);
    printf("  Target received: %lu\n", 
           (unsigned long)g_nodes[4].stats.packets_received);
}

/* Test 3: Large message fragmentation */
static void test_fragmentation_stress(void)
{
    printf("\n=== Test 3: Fragmentation Stress ===\n");
    
    uint8_t payload[STRESS_LARGE_MSG_SIZE];
    uint32_t seq = 0;
    uint64_t start = nx_platform_time_ms();
    uint64_t sent = 0;
    
    test_node_t *sender = &g_nodes[0];
    test_node_t *receiver = &g_nodes[1];
    
    while (nx_platform_time_ms() - start < 5000) {
        generate_payload(payload, sizeof(payload), seq++);
        
        nx_err_t err = nx_node_send_large(&sender->node,
                                          &receiver->identity.short_addr,
                                          payload, sizeof(payload));
        
        if (err == NX_OK) {
            sent++;
        }
        
        nx_node_poll(&receiver->node, 10);
        sleep_ms(10);
    }
    
    printf("  Large messages sent: %lu\n", (unsigned long)sent);
}

/* Test 4: Session stress - rapid key rotations */
static void test_session_stress(void)
{
    printf("\n=== Test 4: Session Stress ===\n");
    
    test_node_t *alice = &g_nodes[0];
    test_node_t *bob = &g_nodes[1];
    
    nx_err_t err = nx_node_session_start(&alice->node, &bob->identity.short_addr);
    printf("  Session start: %s\n", err == NX_OK ? "OK" : "FAIL");
    
    uint8_t payload[128];
    uint64_t start = nx_platform_time_ms();
    uint64_t sent = 0;
    
    while (nx_platform_time_ms() - start < 5000) {
        generate_payload(payload, sizeof(payload), sent);
        
        err = nx_node_send_session(&alice->node, &bob->identity.short_addr,
                                  payload, sizeof(payload));
        
        if (err == NX_OK) {
            sent++;
        }
        
        nx_node_poll(&alice->node, 0);
        nx_node_poll(&bob->node, 0);
    }
    
    printf("  Session messages sent: %lu\n", (unsigned long)sent);
}

/* Test 5: Group encryption stress */
static void test_group_stress(void)
{
    printf("\n=== Test 5: Group Encryption Stress ===\n");
    
    test_node_t *coordinator = &g_nodes[0];
    uint8_t group_key[32] = {0};
    nx_addr_short_t group_id = {{0xAB, 0xCD, 0xEF, 0x00}};
    
    nx_err_t err = nx_node_group_create(&coordinator->node, &group_id, group_key);
    printf("  Group create: %s\n", err == NX_OK ? "OK" : "FAIL");
    
    for (int i = 1; i < 5; i++) {
        err = nx_node_group_add_member(&coordinator->node, &group_id,
                                        &g_nodes[i].identity.short_addr);
        (void)err;
    }
    
    uint8_t payload[NX_MAX_PAYLOAD];
    uint64_t start = nx_platform_time_ms();
    uint64_t sent = 0;
    
    while (nx_platform_time_ms() - start < 5000) {
        generate_payload(payload, sizeof(payload), sent);
        
        err = nx_node_group_send(&coordinator->node, &group_id,
                                  payload, sizeof(payload));
        
        if (err == NX_OK) {
            sent++;
        }
        
        for (int i = 0; i < 5; i++) {
            nx_node_poll(&g_nodes[i].node, 0);
        }
    }
    
    printf("  Group messages sent: %lu\n", (unsigned long)sent);
}

/* Test 6: Adaptive Spreading Factor */
static void test_asf(void)
{
    printf("\n=== Test 6: Adaptive Spreading Factor ===\n");
    
    nx_asf_state_t *asf = nx_asf_create(NX_ASF_STRATEGY_BALANCED, 10);
    if (!asf) {
        printf("  Failed to create ASF state\n");
        return;
    }
    
    printf("  Initial SF: %d\n", nx_asf_get_current_sf(asf));
    printf("  Strategy: %s\n", nx_asf_strategy_name(nx_asf_get_strategy(asf)));
    
    int8_t rssi_values[] = {-70, -75, -80, -90, -100, -110, -100, -90, -80};
    int8_t snr_values[] = {15, 12, 10, 5, 0, -5, 0, 5, 10};
    
    for (int i = 0; i < 9; i++) {
        nx_asf_record_tx(asf, nx_asf_get_current_sf(asf));
        nx_asf_record_rx(asf, rssi_values[i], snr_values[i], 
                         nx_asf_get_current_sf(asf));
        nx_asf_record_ack(asf, i % 3 != 0);
        
        uint8_t sf = nx_asf_get_recommended_sf(asf);
        printf("  Iteration %d: RSSI=%d, SNR=%d -> SF=%d\n", 
               i, rssi_values[i], snr_values[i], sf);
    }
    
    nx_asf_strategy_t strategies[] = {
        NX_ASF_STRATEGY_CONSERVATIVE,
        NX_ASF_STRATEGY_BALANCED,
        NX_ASF_STRATEGY_AGGRESSIVE,
        NX_ASF_STRATEGY_ADAPTIVE
    };
    
    for (size_t i = 0; i < sizeof(strategies)/sizeof(strategies[0]); i++) {
        nx_asf_set_strategy(asf, strategies[i]);
        nx_asf_reset(asf);
        
        nx_asf_record_tx(asf, 10);
        nx_asf_record_rx(asf, -70, 15, 10);
        nx_asf_record_ack(asf, true);
        
        uint8_t sf = nx_asf_get_recommended_sf(asf);
        printf("  %s strategy: SF=%d\n",
               nx_asf_strategy_name(strategies[i]), sf);
    }
    
    uint32_t airtime_sf7 = nx_asf_estimate_airtime(7, 100);
    uint32_t airtime_sf12 = nx_asf_estimate_airtime(12, 100);
    printf("  Airtime 100B @ SF7: %u ms\n", airtime_sf7);
    printf("  Airtime 100B @ SF12: %u ms\n", airtime_sf12);
    printf("  Ratio: %.1fx\n", (float)airtime_sf12 / airtime_sf7);
    
    nx_asf_destroy(asf);
}

/* Print final statistics */
static void print_stats(void)
{
    printf("\n=== Final Statistics ===\n");
    
    uint64_t total_sent = 0, total_recv = 0;
    
    for (int i = 0; i < STRESS_NUM_NODES; i++) {
        test_node_t *n = &g_nodes[i];
        total_sent += n->stats.packets_sent;
        total_recv += n->stats.packets_received;
        
        printf("\nNode %d:\n", i);
        printf("  Packets sent: %lu\n", (unsigned long)n->stats.packets_sent);
        printf("  Packets received: %lu\n", (unsigned long)n->stats.packets_received);
        printf("  Bytes sent: %lu\n", (unsigned long)n->stats.bytes_sent);
    }
    
    printf("\nTotal:\n");
    printf("  Packets sent: %lu\n", (unsigned long)total_sent);
    printf("  Packets received: %lu\n", (unsigned long)total_recv);
    if (total_sent > 0) {
        double delivery_rate = (double)total_recv / total_sent * 100.0;
        printf("  Delivery rate: %.1f%%\n", delivery_rate);
    }
}

/* Initialize test nodes */
static void init_nodes(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
    
    for (int i = 0; i < STRESS_NUM_NODES; i++) {
        test_node_t *n = &g_nodes[i];
        
        nx_err_t err = nx_identity_generate(&n->identity);
        assert(err == NX_OK);
        
        nx_node_config_t cfg = {
            .role = NX_ROLE_RELAY,
            .default_ttl = 8,
            .beacon_interval_ms = 1000,
            .on_data = on_data_cb,
            .user_ctx = n
        };
        
        err = nx_node_init_with_identity(&n->node, &cfg, &n->identity);
        assert(err == NX_OK);
    }
}

/* Cleanup test nodes */
static void cleanup_nodes(void)
{
    for (int i = 0; i < STRESS_NUM_NODES; i++) {
        nx_node_stop(&g_nodes[i].node);
        nx_identity_wipe(&g_nodes[i].identity);
    }
}

int main(void)
{
    printf("NEXUS Protocol Stress Tests\n");
    printf("=========================\n");
    
    g_start_time = nx_platform_time_ms();
    
    init_nodes();
    
    test_high_volume_flood();
    test_multihop_routing();
    test_fragmentation_stress();
    test_session_stress();
    test_group_stress();
    test_asf();
    
    print_stats();
    
    cleanup_nodes();
    
    printf("\nAll stress tests completed.\n");
    return 0;
}
