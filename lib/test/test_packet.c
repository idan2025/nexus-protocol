/*
 * NEXUS Protocol -- Packet Unit Tests
 */
#include "nexus/packet.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s", name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while (0)

static void test_flags_encoding(void)
{
    TEST("flag byte encodes all fields correctly");

    uint8_t f = nx_packet_flags(true, false, NX_PRIO_HIGH, NX_PTYPE_DATA, NX_RTYPE_ROUTED);
    ASSERT(nx_packet_flag_frag(f) == true, "frag");
    ASSERT(nx_packet_flag_exthdr(f) == false, "exthdr");
    ASSERT(nx_packet_flag_prio(f) == NX_PRIO_HIGH, "prio");
    ASSERT(nx_packet_flag_ptype(f) == NX_PTYPE_DATA, "ptype");
    ASSERT(nx_packet_flag_rtype(f) == NX_RTYPE_ROUTED, "rtype");

    PASS();
}

static void test_hop_ttl_encoding(void)
{
    TEST("hop_ttl encodes hop count and TTL");

    uint8_t ht = nx_packet_hop_ttl(3, 12);
    ASSERT(nx_packet_hop_count(ht) == 3, "hop_count");
    ASSERT(nx_packet_ttl(ht) == 12, "ttl");

    /* Max values (4 bits each) */
    ht = nx_packet_hop_ttl(15, 15);
    ASSERT(nx_packet_hop_count(ht) == 15, "max hop");
    ASSERT(nx_packet_ttl(ht) == 15, "max ttl");

    PASS();
}

static void test_serialize_deserialize_roundtrip(void)
{
    TEST("serialize/deserialize roundtrip preserves data");

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                       NX_PTYPE_DATA, NX_RTYPE_DIRECT);
    pkt.header.hop_count = 0;
    pkt.header.ttl = 7;
    pkt.header.dst = (nx_addr_short_t){{0xDE, 0xAD, 0xBE, 0xEF}};
    pkt.header.src = (nx_addr_short_t){{0xCA, 0xFE, 0xBA, 0xBE}};
    pkt.header.seq_id = 0x1234;
    pkt.header.payload_len = 5;
    memcpy(pkt.payload, "hello", 5);

    uint8_t wire[NX_MAX_PACKET];
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(n == NX_HEADER_SIZE + 5, "serialize size");

    nx_packet_t out;
    nx_err_t err = nx_packet_deserialize(wire, (size_t)n, &out);
    ASSERT(err == NX_OK, "deserialize");

    ASSERT(out.header.flags == pkt.header.flags, "flags");
    ASSERT(out.header.hop_count == 0, "hop_count");
    ASSERT(out.header.ttl == 7, "ttl");
    ASSERT(memcmp(out.header.dst.bytes, pkt.header.dst.bytes, 4) == 0, "dst");
    ASSERT(memcmp(out.header.src.bytes, pkt.header.src.bytes, 4) == 0, "src");
    ASSERT(out.header.seq_id == 0x1234, "seq_id");
    ASSERT(out.header.payload_len == 5, "payload_len");
    ASSERT(memcmp(out.payload, "hello", 5) == 0, "payload");

    PASS();
}

static void test_empty_payload(void)
{
    TEST("serialize/deserialize with empty payload");

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.flags = nx_packet_flags(false, false, NX_PRIO_LOW,
                                       NX_PTYPE_ACK, NX_RTYPE_DIRECT);
    pkt.header.ttl = 1;
    pkt.header.payload_len = 0;

    uint8_t wire[NX_HEADER_SIZE];
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(n == NX_HEADER_SIZE, "size");

    nx_packet_t out;
    ASSERT(nx_packet_deserialize(wire, (size_t)n, &out) == NX_OK, "deser");
    ASSERT(out.header.payload_len == 0, "len");

    PASS();
}

static void test_wire_size(void)
{
    TEST("nx_packet_wire_size returns correct size");

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.payload_len = 100;
    ASSERT(nx_packet_wire_size(&pkt) == NX_HEADER_SIZE + 100, "size");

    PASS();
}

static void test_buffer_too_small(void)
{
    TEST("serialize rejects too-small buffer");

    nx_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.payload_len = 10;

    uint8_t wire[5]; /* Too small */
    int n = nx_packet_serialize(&pkt, wire, sizeof(wire));
    ASSERT(n == NX_ERR_BUFFER_TOO_SMALL, "should fail");

    PASS();
}

static void test_deserialize_truncated(void)
{
    TEST("deserialize rejects truncated buffer");

    uint8_t wire[5] = {0}; /* Less than header */
    nx_packet_t pkt;
    ASSERT(nx_packet_deserialize(wire, sizeof(wire), &pkt) == NX_ERR_BUFFER_TOO_SMALL,
           "should fail");

    PASS();
}

static void test_all_flag_combinations(void)
{
    TEST("all flag field combinations roundtrip");

    for (int frag = 0; frag <= 1; frag++) {
        for (int ext = 0; ext <= 1; ext++) {
            for (int prio = 0; prio < 4; prio++) {
                for (int ptype = 0; ptype < 4; ptype++) {
                    for (int rtype = 0; rtype < 4; rtype++) {
                        uint8_t f = nx_packet_flags(
                            (bool)frag, (bool)ext,
                            (nx_prio_t)prio, (nx_ptype_t)ptype, (nx_rtype_t)rtype);
                        ASSERT(nx_packet_flag_frag(f)   == (bool)frag, "frag");
                        ASSERT(nx_packet_flag_exthdr(f) == (bool)ext, "ext");
                        ASSERT(nx_packet_flag_prio(f)   == (nx_prio_t)prio, "prio");
                        ASSERT(nx_packet_flag_ptype(f)  == (nx_ptype_t)ptype, "ptype");
                        ASSERT(nx_packet_flag_rtype(f)  == (nx_rtype_t)rtype, "rtype");
                    }
                }
            }
        }
    }
    PASS();
}

int main(void)
{
    printf("=== NEXUS Packet Tests ===\n");

    test_flags_encoding();
    test_hop_ttl_encoding();
    test_serialize_deserialize_roundtrip();
    test_empty_payload();
    test_wire_size();
    test_buffer_too_small();
    test_deserialize_truncated();
    test_all_flag_combinations();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
