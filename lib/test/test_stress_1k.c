/*
 * NEXUS Protocol -- 1000+ Node Stress Test
 *
 * Validates identity generation, neighbor injection, route table
 * population, and crypto operations at scale (1024 nodes).
 */

#include "nexus/node.h"
#include "nexus/identity.h"
#include "nexus/crypto.h"
#include "nexus/packet.h"
#include "nexus/platform.h"
#include "nexus/lora_asf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define NUM_NODES 1024

typedef struct {
    nx_identity_t identity;
    nx_addr_short_t short_addr;
} light_node_t;

static light_node_t *g_nodes;

/* ── Test 1: Generate 1024 unique identities ──────────────────────── */
static void test_identity_generation(void)
{
    printf("  Generating %d identities... ", NUM_NODES);
    fflush(stdout);

    uint64_t start = nx_platform_time_ms();

    for (int i = 0; i < NUM_NODES; i++) {
        nx_err_t err = nx_identity_generate(&g_nodes[i].identity);
        assert(err == NX_OK);
        memcpy(&g_nodes[i].short_addr, &g_nodes[i].identity.short_addr,
               sizeof(nx_addr_short_t));
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    printf("done (%lu ms)\n", (unsigned long)elapsed);
}

/* ── Test 2: Verify all addresses are unique ──────────────────────── */
static void test_address_uniqueness(void)
{
    printf("  Checking address uniqueness... ");
    fflush(stdout);

    int collisions = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            if (memcmp(g_nodes[i].short_addr.bytes,
                       g_nodes[j].short_addr.bytes, 4) == 0) {
                collisions++;
            }
        }
    }

    printf("%d collisions in %d addresses\n", collisions, NUM_NODES);
    assert(collisions == 0);
}

/* ── Test 3: AEAD crypto throughput at scale ──────────────────────── */
static void test_crypto_throughput(void)
{
    printf("  Crypto throughput (%d AEAD lock+unlock)... ", NUM_NODES);
    fflush(stdout);

    uint8_t key[32], nonce[24], plaintext[242], ciphertext[242], mac[16];
    memset(key, 0xAA, sizeof(key));
    memset(nonce, 0xBB, sizeof(nonce));
    memset(plaintext, 0xCC, sizeof(plaintext));

    uint64_t start = nx_platform_time_ms();

    for (int i = 0; i < NUM_NODES; i++) {
        memcpy(key, g_nodes[i].identity.sign_public, 32);
        nonce[0] = (uint8_t)(i & 0xFF);
        nonce[1] = (uint8_t)((i >> 8) & 0xFF);

        nx_err_t err = nx_crypto_aead_lock(
            key, nonce, NULL, 0,
            plaintext, sizeof(plaintext),
            ciphertext, mac);
        assert(err == NX_OK);

        uint8_t decrypted[242];
        err = nx_crypto_aead_unlock(
            key, nonce, mac, NULL, 0,
            ciphertext, sizeof(plaintext),
            decrypted);
        assert(err == NX_OK);
        assert(memcmp(plaintext, decrypted, sizeof(plaintext)) == 0);
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    double rate = (double)NUM_NODES / ((double)elapsed / 1000.0);
    printf("done (%lu ms, %.0f ops/sec)\n", (unsigned long)elapsed, rate);
}

/* ── Test 4: X25519 key exchange at scale ─────────────────────────── */
static void test_key_exchange_throughput(void)
{
    printf("  X25519 key exchange (%d pairs)... ", NUM_NODES - 1);
    fflush(stdout);

    uint64_t start = nx_platform_time_ms();
    uint8_t shared_a[32], shared_b[32];

    for (int i = 0; i < NUM_NODES - 1; i++) {
        nx_err_t err = nx_crypto_x25519_derive(
            g_nodes[i].identity.x25519_secret,
            g_nodes[i + 1].identity.x25519_public,
            shared_a);
        assert(err == NX_OK);

        err = nx_crypto_x25519_derive(
            g_nodes[i + 1].identity.x25519_secret,
            g_nodes[i].identity.x25519_public,
            shared_b);
        assert(err == NX_OK);

        assert(memcmp(shared_a, shared_b, 32) == 0);
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    double rate = (double)(NUM_NODES - 1) / ((double)elapsed / 1000.0);
    printf("done (%lu ms, %.0f exchanges/sec)\n", (unsigned long)elapsed, rate);
}

/* ── Test 5: Route table population ───────────────────────────────── */
static void test_route_table_stress(void)
{
    printf("  Route table stress (fill 32 neighbors + 64 routes)... ");
    fflush(stdout);

    nx_node_t node;
    nx_node_config_t cfg = {
        .role = NX_ROLE_RELAY,
        .default_ttl = 8,
        .beacon_interval_ms = 999999999,
    };
    nx_err_t err = nx_node_init_with_identity(&node, &cfg, &g_nodes[0].identity);
    assert(err == NX_OK);

    uint64_t now = nx_platform_time_ms();

    int neighbors_added = 0;
    for (int i = 1; i < NUM_NODES && neighbors_added < 32; i++) {
        int rc = nx_neighbor_update(
            &node.route_table,
            &g_nodes[i].short_addr,
            &g_nodes[i].identity.full_addr,
            g_nodes[i].identity.sign_public,
            g_nodes[i].identity.x25519_public,
            0, 0, now);
        if (rc == 0) neighbors_added++;
    }

    int routes_added = 0;
    for (int i = 33; i < NUM_NODES && routes_added < 64; i++) {
        int rc = nx_route_update(
            &node.route_table,
            &g_nodes[i].short_addr,
            &g_nodes[1].short_addr,
            2, 1, now);
        if (rc == 0) routes_added++;
    }

    printf("neighbors=%d routes=%d\n", neighbors_added, routes_added);
    assert(neighbors_added == 32);
    assert(routes_added == 64);

    nx_node_stop(&node);
}

/* ── Test 6: Packet serialization throughput ──────────────────────── */
static void test_packet_throughput(void)
{
    printf("  Packet serialize/deserialize (10K ops)... ");
    fflush(stdout);

    uint64_t start = nx_platform_time_ms();
    int count = 10000;

    for (int i = 0; i < count; i++) {
        nx_packet_t pkt;
        memset(&pkt, 0, sizeof(pkt));

        pkt.header.flags = nx_packet_flags(false, false,
                                            NX_PRIO_NORMAL, NX_PTYPE_DATA, 0);
        pkt.header.hop_count = 0;
        pkt.header.ttl = 7;
        pkt.header.seq_id = (uint16_t)(i & 0xFFFF);
        memcpy(pkt.header.src.bytes,
               g_nodes[i % NUM_NODES].short_addr.bytes, 4);
        memcpy(pkt.header.dst.bytes,
               g_nodes[(i + 1) % NUM_NODES].short_addr.bytes, 4);
        pkt.header.payload_len = 100;
        memset(pkt.payload, 0xDD, 100);

        uint8_t wire[NX_HEADER_SIZE + NX_MAX_PAYLOAD];
        int wrote = nx_packet_serialize(&pkt, wire, sizeof(wire));
        assert(wrote > 0);

        nx_packet_t pkt2;
        nx_err_t err = nx_packet_deserialize(wire, (size_t)wrote, &pkt2);
        assert(err == NX_OK);
        assert(pkt2.header.seq_id == pkt.header.seq_id);
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    double rate = (double)count / ((double)elapsed / 1000.0);
    printf("done (%lu ms, %.0f ops/sec)\n", (unsigned long)elapsed, rate);
}

/* ── Test 7: ASF strategy sweep ───────────────────────────────────── */
static void test_asf_sweep(void)
{
    printf("  ASF strategy sweep (4 x 256 iterations)... ");
    fflush(stdout);

    nx_asf_strategy_t strategies[] = {
        NX_ASF_STRATEGY_CONSERVATIVE,
        NX_ASF_STRATEGY_BALANCED,
        NX_ASF_STRATEGY_AGGRESSIVE,
        NX_ASF_STRATEGY_ADAPTIVE,
    };

    uint64_t start = nx_platform_time_ms();

    for (int s = 0; s < 4; s++) {
        nx_asf_state_t *asf = nx_asf_create(strategies[s], 10);
        assert(asf != NULL);

        for (int i = 0; i < 256; i++) {
            int8_t rssi = (int8_t)(-60 - (i % 60));
            int8_t snr = (int8_t)(20 - (i % 30));
            bool ack = (i % 5 != 0);

            nx_asf_record_tx(asf, nx_asf_get_current_sf(asf));
            nx_asf_record_rx(asf, rssi, snr, nx_asf_get_current_sf(asf));
            nx_asf_record_ack(asf, ack);

            uint8_t sf = nx_asf_get_recommended_sf(asf);
            assert(sf >= 7 && sf <= 12);
        }

        nx_asf_destroy(asf);
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    printf("done (%lu ms)\n", (unsigned long)elapsed);
}

/* ── Test 8: Full node init/stop cycle at scale ───────────────────── */
static void test_node_lifecycle_batch(void)
{
    printf("  Node lifecycle (128 nodes x 8 batches = 1024)... ");
    fflush(stdout);

    uint64_t start = nx_platform_time_ms();

    for (int batch = 0; batch < 8; batch++) {
        nx_node_t *nodes = calloc(128, sizeof(nx_node_t));
        assert(nodes != NULL);

        for (int i = 0; i < 128; i++) {
            int idx = batch * 128 + i;
            nx_node_config_t cfg = {
                .role = NX_ROLE_LEAF,
                .default_ttl = 7,
                .beacon_interval_ms = 999999999,
            };
            nx_err_t err = nx_node_init_with_identity(
                &nodes[i], &cfg, &g_nodes[idx].identity);
            assert(err == NX_OK);
        }

        for (int i = 0; i < 128; i++) {
            nx_node_stop(&nodes[i]);
        }

        free(nodes);
    }

    uint64_t elapsed = nx_platform_time_ms() - start;
    printf("done (%lu ms)\n", (unsigned long)elapsed);
}

int main(void)
{
    printf("NEXUS 1K Node Stress Test (%d nodes)\n", NUM_NODES);
    printf("====================================\n\n");

    g_nodes = calloc(NUM_NODES, sizeof(light_node_t));
    if (!g_nodes) {
        printf("FATAL: could not allocate %d nodes\n", NUM_NODES);
        return 1;
    }

    uint64_t start = nx_platform_time_ms();

    test_identity_generation();
    test_address_uniqueness();
    test_crypto_throughput();
    test_key_exchange_throughput();
    test_route_table_stress();
    test_packet_throughput();
    test_asf_sweep();
    test_node_lifecycle_batch();

    uint64_t total = nx_platform_time_ms() - start;
    printf("\nAll 8 tests passed in %lu ms\n", (unsigned long)total);

    for (int i = 0; i < NUM_NODES; i++) {
        nx_identity_wipe(&g_nodes[i].identity);
    }
    free(g_nodes);

    return 0;
}
