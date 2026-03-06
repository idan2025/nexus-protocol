/*
 * NEXUS Protocol -- Fragment Unit Tests
 */
#include "nexus/fragment.h"
#include "nexus/packet.h"
#include "nexus/identity.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-50s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

static nx_header_t make_base_hdr(void)
{
    nx_header_t h;
    memset(&h, 0, sizeof(h));
    h.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                              NX_PTYPE_DATA, NX_RTYPE_FLOOD);
    h.ttl = 7;
    h.dst = (nx_addr_short_t){{0xDE, 0xAD, 0xBE, 0xEF}};
    h.src = (nx_addr_short_t){{0xCA, 0xFE, 0xBA, 0xBE}};
    h.seq_id = 1;
    return h;
}

static void test_exthdr_roundtrip(void)
{
    TEST("fragment exthdr encode/decode roundtrip");

    nx_frag_header_t fh = { .frag_id = 0x1234, .frag_index = 5, .frag_total = 10 };
    uint8_t buf[8];
    int n = nx_frag_encode_exthdr(&fh, buf, sizeof(buf));
    ASSERT(n == NX_FRAG_EXTHDR_SIZE, "encode size");

    nx_frag_header_t out;
    ASSERT(nx_frag_decode_exthdr(buf, (size_t)n, &out) == NX_OK, "decode");
    ASSERT(out.frag_id == 0x1234, "id");
    ASSERT(out.frag_index == 5, "index");
    ASSERT(out.frag_total == 10, "total");

    PASS();
}

static void test_no_fragmentation_needed(void)
{
    TEST("small message passes through unfragmented");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    nx_header_t base = make_base_hdr();
    uint8_t data[100];
    memset(data, 0xAB, sizeof(data));

    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;
    ASSERT(nx_frag_split(&fb, &base, data, sizeof(data), pkts, &count) == NX_OK,
           "split");
    ASSERT(count == 1, "single packet");
    ASSERT(!nx_packet_flag_frag(pkts[0].header.flags), "no FRAG flag");
    ASSERT(!nx_packet_flag_exthdr(pkts[0].header.flags), "no EXTHDR flag");
    ASSERT(pkts[0].header.payload_len == 100, "payload_len");
    ASSERT(memcmp(pkts[0].payload, data, 100) == 0, "data");

    PASS();
}

static void test_fragment_and_reassemble(void)
{
    TEST("fragment and reassemble 1000-byte message");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    /* Create 1000-byte test message */
    uint8_t msg[1000];
    for (int i = 0; i < 1000; i++) msg[i] = (uint8_t)(i & 0xFF);

    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;

    ASSERT(nx_frag_split(&fb, &base, msg, sizeof(msg), pkts, &count) == NX_OK,
           "split");
    ASSERT(count > 1, "multiple fragments");
    ASSERT(count <= NX_FRAG_MAX_COUNT, "within limit");

    /* All but last should have FRAG flag, all should have EXTHDR */
    for (int i = 0; i < count; i++) {
        ASSERT(nx_packet_flag_exthdr(pkts[i].header.flags), "EXTHDR set");
        if (i < count - 1) {
            ASSERT(nx_packet_flag_frag(pkts[i].header.flags), "FRAG on non-last");
        } else {
            ASSERT(!nx_packet_flag_frag(pkts[i].header.flags), "no FRAG on last");
        }
    }

    /* Reassemble in order */
    nx_frag_buffer_t rfb;
    nx_frag_init(&rfb);

    uint8_t result[2000];
    size_t  result_len = 0;

    for (int i = 0; i < count; i++) {
        nx_err_t err = nx_frag_receive(&rfb, &pkts[i], result, sizeof(result),
                                       &result_len, 1000);
        ASSERT(err == NX_OK, "receive");
        if (i < count - 1) {
            ASSERT(result_len == 0, "not complete yet");
        }
    }

    ASSERT(result_len == 1000, "complete size");
    ASSERT(memcmp(result, msg, 1000) == 0, "data matches");

    PASS();
}

static void test_out_of_order(void)
{
    TEST("reassemble with out-of-order delivery");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    uint8_t msg[600];
    for (int i = 0; i < 600; i++) msg[i] = (uint8_t)(i * 3);

    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;
    nx_frag_split(&fb, &base, msg, sizeof(msg), pkts, &count);
    ASSERT(count > 1, "multi");

    /* Deliver in reverse order */
    nx_frag_buffer_t rfb;
    nx_frag_init(&rfb);
    uint8_t result[2000];
    size_t  result_len = 0;

    for (int i = count - 1; i >= 0; i--) {
        nx_frag_receive(&rfb, &pkts[i], result, sizeof(result),
                        &result_len, 1000);
    }

    ASSERT(result_len == 600, "complete");
    ASSERT(memcmp(result, msg, 600) == 0, "data");

    PASS();
}

static void test_duplicate_fragment(void)
{
    TEST("duplicate fragment is handled gracefully");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    uint8_t msg[500];
    memset(msg, 0x55, sizeof(msg));

    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;
    nx_frag_split(&fb, &base, msg, sizeof(msg), pkts, &count);

    nx_frag_buffer_t rfb;
    nx_frag_init(&rfb);
    uint8_t result[2000];
    size_t  result_len = 0;

    /* Send first fragment twice */
    nx_frag_receive(&rfb, &pkts[0], result, sizeof(result), &result_len, 1000);
    nx_frag_receive(&rfb, &pkts[0], result, sizeof(result), &result_len, 1000);
    ASSERT(result_len == 0, "not complete from dup");

    /* Send remaining */
    for (int i = 1; i < count; i++) {
        nx_frag_receive(&rfb, &pkts[i], result, sizeof(result),
                        &result_len, 1000);
    }

    ASSERT(result_len == 500, "complete");
    ASSERT(memcmp(result, msg, 500) == 0, "data");

    PASS();
}

static void test_reassembly_timeout(void)
{
    TEST("incomplete reassembly expires");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    uint8_t msg[500];
    memset(msg, 0xAA, sizeof(msg));

    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;
    nx_frag_split(&fb, &base, msg, sizeof(msg), pkts, &count);

    nx_frag_buffer_t rfb;
    nx_frag_init(&rfb);
    uint8_t result[2000];
    size_t  result_len = 0;

    /* Only send first fragment */
    nx_frag_receive(&rfb, &pkts[0], result, sizeof(result), &result_len, 1000);

    /* Expire */
    nx_frag_expire(&rfb, 1000 + NX_FRAG_TIMEOUT_MS + 1);

    /* Verify slot is freed -- send same fragment again, should start fresh */
    nx_frag_receive(&rfb, &pkts[0], result, sizeof(result), &result_len,
                    1000 + NX_FRAG_TIMEOUT_MS + 2);
    ASSERT(result_len == 0, "not complete");

    PASS();
}

static void test_max_message_size(void)
{
    TEST("maximum message size fragments correctly");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    uint8_t msg[NX_FRAG_MAX_MESSAGE];
    for (size_t i = 0; i < sizeof(msg); i++)
        msg[i] = (uint8_t)(i % 251);

    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;
    ASSERT(nx_frag_split(&fb, &base, msg, sizeof(msg), pkts, &count) == NX_OK,
           "split max");
    ASSERT(count == NX_FRAG_MAX_COUNT, "16 fragments");

    /* Reassemble */
    nx_frag_buffer_t rfb;
    nx_frag_init(&rfb);
    uint8_t result[NX_FRAG_MAX_MESSAGE + 100];
    size_t result_len = 0;

    for (int i = 0; i < count; i++) {
        nx_frag_receive(&rfb, &pkts[i], result, sizeof(result),
                        &result_len, 1000);
    }

    ASSERT(result_len == NX_FRAG_MAX_MESSAGE, "full size");
    ASSERT(memcmp(result, msg, NX_FRAG_MAX_MESSAGE) == 0, "data");

    PASS();
}

static void test_too_large_rejected(void)
{
    TEST("message exceeding max is rejected");

    nx_frag_buffer_t fb;
    nx_frag_init(&fb);

    uint8_t big[NX_FRAG_MAX_MESSAGE + 1];
    nx_header_t base = make_base_hdr();
    nx_packet_t pkts[NX_FRAG_MAX_COUNT];
    int count = 0;

    ASSERT(nx_frag_split(&fb, &base, big, sizeof(big), pkts, &count)
           == NX_ERR_BUFFER_TOO_SMALL, "rejected");

    PASS();
}

int main(void)
{
    printf("=== NEXUS Fragment Tests ===\n");

    test_exthdr_roundtrip();
    test_no_fragmentation_needed();
    test_fragment_and_reassemble();
    test_out_of_order();
    test_duplicate_fragment();
    test_reassembly_timeout();
    test_max_message_size();
    test_too_large_rejected();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
