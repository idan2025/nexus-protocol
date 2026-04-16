/*
 * NEXUS Protocol -- Message Ring Buffer Tests
 */
#include "nexus/msgring.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-55s", name); } while (0)
#define PASS()     do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while (0)

static nx_addr_short_t make_addr(uint32_t v)
{
    nx_addr_short_t a;
    a.bytes[0] = (uint8_t)(v >> 24);
    a.bytes[1] = (uint8_t)(v >> 16);
    a.bytes[2] = (uint8_t)(v >> 8);
    a.bytes[3] = (uint8_t)(v);
    return a;
}

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_init_empty(void)
{
    TEST("init produces empty ring");
    nx_msgring_t ring;
    nx_msgring_init(&ring);
    ASSERT(nx_msgring_count(&ring) == 0, "count should be 0");
    ASSERT(nx_msgring_get(&ring, 0) == NULL, "get(0) should be NULL");
    PASS();
}

static void test_push_and_get(void)
{
    TEST("push + get returns correct data");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    nx_addr_short_t src = make_addr(0xDEAD0001);
    const uint8_t data[] = "hello nexus";
    nx_msgring_push(&ring, &src, 1000, data, sizeof(data) - 1);

    ASSERT(nx_msgring_count(&ring) == 1, "count");
    const nx_msgring_entry_t *e = nx_msgring_get(&ring, 0);
    ASSERT(e != NULL, "entry");
    ASSERT(e->len == sizeof(data) - 1, "len");
    ASSERT(memcmp(e->data, "hello nexus", 11) == 0, "data");
    ASSERT(e->timestamp_s == 1000, "timestamp");
    ASSERT(memcmp(e->src.bytes, src.bytes, 4) == 0, "src addr");
    PASS();
}

static void test_ordering(void)
{
    TEST("get(0) is newest, get(n-1) is oldest");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    for (int i = 0; i < 5; i++) {
        nx_addr_short_t src = make_addr((uint32_t)i);
        uint8_t d = (uint8_t)i;
        nx_msgring_push(&ring, &src, (uint32_t)(100 + i), &d, 1);
    }

    ASSERT(nx_msgring_count(&ring) == 5, "count");

    /* Most recent (i=4) at index 0 */
    const nx_msgring_entry_t *newest = nx_msgring_get(&ring, 0);
    ASSERT(newest && newest->data[0] == 4, "newest");
    ASSERT(newest->timestamp_s == 104, "newest ts");

    /* Oldest (i=0) at index 4 */
    const nx_msgring_entry_t *oldest = nx_msgring_get(&ring, 4);
    ASSERT(oldest && oldest->data[0] == 0, "oldest");
    ASSERT(oldest->timestamp_s == 100, "oldest ts");
    PASS();
}

static void test_wrap_around(void)
{
    TEST("ring wraps and overwrites oldest entries");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    /* Fill with CAPACITY+8 messages */
    for (int i = 0; i < NX_MSGRING_CAPACITY + 8; i++) {
        nx_addr_short_t src = make_addr((uint32_t)i);
        uint8_t d = (uint8_t)(i & 0xFF);
        nx_msgring_push(&ring, &src, (uint32_t)i, &d, 1);
    }

    ASSERT(nx_msgring_count(&ring) == NX_MSGRING_CAPACITY, "capped at capacity");

    /* Newest should be CAPACITY+7 */
    const nx_msgring_entry_t *newest = nx_msgring_get(&ring, 0);
    ASSERT(newest && newest->data[0] == (uint8_t)((NX_MSGRING_CAPACITY + 7) & 0xFF),
           "newest after wrap");

    /* Oldest should be 8 (first 0..7 overwritten) */
    const nx_msgring_entry_t *oldest = nx_msgring_get(&ring, NX_MSGRING_CAPACITY - 1);
    ASSERT(oldest && oldest->data[0] == 8, "oldest after wrap");
    PASS();
}

static void test_clear(void)
{
    TEST("clear empties the ring");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    nx_addr_short_t src = make_addr(1);
    uint8_t d = 42;
    nx_msgring_push(&ring, &src, 1, &d, 1);
    ASSERT(nx_msgring_count(&ring) == 1, "pre-clear");

    nx_msgring_clear(&ring);
    ASSERT(nx_msgring_count(&ring) == 0, "post-clear");
    ASSERT(nx_msgring_get(&ring, 0) == NULL, "get after clear");
    PASS();
}

static void test_max_payload(void)
{
    TEST("push clamps to NX_MAX_PAYLOAD");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    nx_addr_short_t src = make_addr(1);
    uint8_t big[300];
    memset(big, 0xAA, sizeof(big));
    nx_msgring_push(&ring, &src, 1, big, sizeof(big));

    const nx_msgring_entry_t *e = nx_msgring_get(&ring, 0);
    ASSERT(e != NULL, "entry");
    ASSERT(e->len == NX_MAX_PAYLOAD, "clamped to max");
    PASS();
}

static void test_serialize_empty(void)
{
    TEST("serialize empty ring produces header only");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    uint8_t buf[NX_MSGRING_BLOB_HEADER];
    size_t written = 0;
    nx_err_t err = nx_msgring_serialize(&ring, buf, sizeof(buf), &written);
    ASSERT(err == NX_OK, "serialize ok");
    ASSERT(written == NX_MSGRING_BLOB_HEADER, "header only");
    ASSERT(buf[5] == 0, "count=0");
    PASS();
}

static void test_serialize_deserialize_roundtrip(void)
{
    TEST("serialize/deserialize roundtrip preserves data");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    /* Push 5 messages with varied sizes */
    for (int i = 0; i < 5; i++) {
        nx_addr_short_t src = make_addr((uint32_t)(0xAA000000 | i));
        uint8_t data[32];
        int dlen = 4 + i * 3;  /* 4, 7, 10, 13, 16 bytes */
        memset(data, (uint8_t)(0x10 + i), (size_t)dlen);
        nx_msgring_push(&ring, &src, (uint32_t)(2000 + i), data, (size_t)dlen);
    }

    /* Serialize */
    uint8_t blob[NX_MSGRING_BLOB_MAX];
    size_t blob_len = 0;
    ASSERT(nx_msgring_serialize(&ring, blob, sizeof(blob), &blob_len) == NX_OK,
           "serialize");

    /* Deserialize into a fresh ring */
    nx_msgring_t ring2;
    ASSERT(nx_msgring_deserialize(&ring2, blob, blob_len) == NX_OK,
           "deserialize");

    ASSERT(nx_msgring_count(&ring2) == 5, "count matches");

    /* Verify each entry (newest first) */
    for (int i = 0; i < 5; i++) {
        const nx_msgring_entry_t *e1 = nx_msgring_get(&ring, i);
        const nx_msgring_entry_t *e2 = nx_msgring_get(&ring2, i);
        ASSERT(e1 && e2, "both non-null");
        ASSERT(e1->len == e2->len, "len match");
        ASSERT(e1->timestamp_s == e2->timestamp_s, "ts match");
        ASSERT(memcmp(e1->src.bytes, e2->src.bytes, 4) == 0, "src match");
        ASSERT(memcmp(e1->data, e2->data, e1->len) == 0, "data match");
    }
    PASS();
}

static void test_serialize_full_ring(void)
{
    TEST("serialize/deserialize full wrapped ring");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    /* Overfill to force wrap */
    for (int i = 0; i < NX_MSGRING_CAPACITY + 10; i++) {
        nx_addr_short_t src = make_addr((uint32_t)i);
        uint8_t d = (uint8_t)(i & 0xFF);
        nx_msgring_push(&ring, &src, (uint32_t)i, &d, 1);
    }

    uint8_t blob[NX_MSGRING_BLOB_MAX];
    size_t blob_len = 0;
    ASSERT(nx_msgring_serialize(&ring, blob, sizeof(blob), &blob_len) == NX_OK,
           "serialize full");

    nx_msgring_t ring2;
    ASSERT(nx_msgring_deserialize(&ring2, blob, blob_len) == NX_OK,
           "deserialize full");
    ASSERT(nx_msgring_count(&ring2) == NX_MSGRING_CAPACITY, "count");

    /* Check newest and oldest match */
    const nx_msgring_entry_t *n1 = nx_msgring_get(&ring, 0);
    const nx_msgring_entry_t *n2 = nx_msgring_get(&ring2, 0);
    ASSERT(n1 && n2, "newest");
    ASSERT(n1->data[0] == n2->data[0], "newest data");

    const nx_msgring_entry_t *o1 = nx_msgring_get(&ring, NX_MSGRING_CAPACITY - 1);
    const nx_msgring_entry_t *o2 = nx_msgring_get(&ring2, NX_MSGRING_CAPACITY - 1);
    ASSERT(o1 && o2, "oldest");
    ASSERT(o1->data[0] == o2->data[0], "oldest data");
    PASS();
}

static void test_deserialize_bad_magic(void)
{
    TEST("deserialize rejects bad magic");
    nx_msgring_t ring;
    uint8_t bad[] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
    ASSERT(nx_msgring_deserialize(&ring, bad, sizeof(bad)) == NX_ERR_INVALID_ARG,
           "bad magic");
    PASS();
}

static void test_deserialize_truncated(void)
{
    TEST("deserialize rejects truncated blob");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    nx_addr_short_t src = make_addr(1);
    uint8_t d = 99;
    nx_msgring_push(&ring, &src, 500, &d, 1);

    uint8_t blob[NX_MSGRING_BLOB_MAX];
    size_t blob_len = 0;
    ASSERT(nx_msgring_serialize(&ring, blob, sizeof(blob), &blob_len) == NX_OK,
           "serialize");

    /* Truncate to header + partial entry */
    nx_msgring_t ring2;
    ASSERT(nx_msgring_deserialize(&ring2, blob, NX_MSGRING_BLOB_HEADER + 3)
           == NX_ERR_BUFFER_TOO_SMALL, "truncated");
    ASSERT(nx_msgring_count(&ring2) == 0, "wiped on error");
    PASS();
}

static void test_buffer_too_small(void)
{
    TEST("serialize rejects too-small buffer");
    nx_msgring_t ring;
    nx_msgring_init(&ring);

    nx_addr_short_t src = make_addr(1);
    uint8_t d = 1;
    nx_msgring_push(&ring, &src, 1, &d, 1);

    uint8_t tiny[4];
    size_t written = 0;
    ASSERT(nx_msgring_serialize(&ring, tiny, sizeof(tiny), &written)
           == NX_ERR_BUFFER_TOO_SMALL, "too small");
    PASS();
}

static void test_null_safety(void)
{
    TEST("null args handled gracefully");
    nx_msgring_init(NULL);
    nx_msgring_push(NULL, NULL, 0, NULL, 0);
    ASSERT(nx_msgring_count(NULL) == 0, "null count");
    ASSERT(nx_msgring_get(NULL, 0) == NULL, "null get");
    nx_msgring_clear(NULL);

    size_t out = 0;
    ASSERT(nx_msgring_serialize(NULL, NULL, 0, &out) == NX_ERR_INVALID_ARG,
           "null serialize");
    nx_msgring_t ring;
    ASSERT(nx_msgring_deserialize(&ring, NULL, 0) == NX_ERR_INVALID_ARG,
           "null deserialize");
    PASS();
}

int main(void)
{
    printf("=== NEXUS Message Ring Buffer Tests ===\n");

    test_init_empty();
    test_push_and_get();
    test_ordering();
    test_wrap_around();
    test_clear();
    test_max_payload();
    test_serialize_empty();
    test_serialize_deserialize_roundtrip();
    test_serialize_full_ring();
    test_deserialize_bad_magic();
    test_deserialize_truncated();
    test_buffer_too_small();
    test_null_safety();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
