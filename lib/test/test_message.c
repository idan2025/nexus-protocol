/*
 * NEXUS Protocol -- Message Format Unit Tests
 */
#include "nexus/message.h"
#include "nexus/identity.h"
#include "nexus/platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-55s", name); \
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

/* ── Builder + Parser round-trip ────────────────────────────────────── */

static void test_text_message_roundtrip(void)
{
    TEST("text message build + parse roundtrip");

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Hello NEXUS mesh!", NULL);
    ASSERT(len > 0, "build_text returned 0");
    ASSERT(len <= sizeof(buf), "len too large");

    nx_message_t msg;
    nx_err_t err = nx_msg_parse(buf, len, &msg);
    ASSERT(err == NX_OK, "parse failed");
    ASSERT(msg.version == NX_MSG_VERSION, "wrong version");
    ASSERT(msg.type == NX_MSG_TEXT, "wrong type");
    ASSERT(msg.field_count >= 2, "expected at least 2 fields (msg_id + text)");

    size_t text_len;
    const char *text = nx_msg_get_text(&msg, &text_len);
    ASSERT(text != NULL, "text not found");
    ASSERT(text_len == 17, "wrong text length");
    ASSERT(memcmp(text, "Hello NEXUS mesh!", 17) == 0, "text mismatch");

    /* Verify message ID exists */
    nx_msg_id_t mid;
    err = nx_msg_get_msg_id(&msg, &mid);
    ASSERT(err == NX_OK, "msg_id not found");

    PASS();
}

static void test_text_with_reply(void)
{
    TEST("text message with reply-to field");

    nx_msg_id_t reply_id;
    nx_msg_id_generate(&reply_id);

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Yes!", &reply_id);
    ASSERT(len > 0, "build_text returned 0");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.flags & NX_MSG_FLAG_REPLY, "reply flag not set");

    /* Find reply-to field */
    const nx_msg_field_t *f = nx_msg_find_field(&msg, NX_FIELD_REPLY_TO);
    ASSERT(f != NULL, "reply_to field not found");
    ASSERT(f->len == 4, "wrong reply_to length");
    ASSERT(memcmp(f->data, reply_id.bytes, 4) == 0, "reply_to mismatch");

    PASS();
}

static void test_location_message(void)
{
    TEST("location message build + parse roundtrip");

    uint8_t buf[256];
    size_t len = nx_msg_build_location(buf, sizeof(buf),
                                        40.7128, -74.0060, 10, 5);
    ASSERT(len > 0, "build_location returned 0");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_LOCATION, "wrong type");

    double lat, lon;
    int16_t alt;
    uint8_t acc;
    ASSERT(nx_msg_get_location(&msg, &lat, &lon, &alt, &acc) == NX_OK, "get_location failed");
    ASSERT(fabs(lat - 40.7128) < 0.0001, "latitude mismatch");
    ASSERT(fabs(lon - (-74.0060)) < 0.0001, "longitude mismatch");
    ASSERT(alt == 10, "altitude mismatch");
    ASSERT(acc == 5, "accuracy mismatch");

    PASS();
}

static void test_ack_message(void)
{
    TEST("delivery ACK message");

    nx_msg_id_t ack_id;
    nx_msg_id_generate(&ack_id);

    uint8_t buf[64];
    size_t len = nx_msg_build_ack(buf, sizeof(buf), &ack_id);
    ASSERT(len > 0, "build_ack returned 0");
    ASSERT(len < 20, "ack too large"); /* ACK should be very small */

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_ACK, "wrong type");

    nx_msg_id_t parsed_id;
    ASSERT(nx_msg_get_msg_id(&msg, &parsed_id) == NX_OK, "msg_id not found");
    ASSERT(nx_msg_id_cmp(&ack_id, &parsed_id) == 0, "msg_id mismatch");

    PASS();
}

static void test_reaction_message(void)
{
    TEST("reaction message");

    nx_msg_id_t target;
    nx_msg_id_generate(&target);

    uint8_t buf[128];
    size_t len = nx_msg_build_reaction(buf, sizeof(buf), &target, "thumbsup");
    ASSERT(len > 0, "build_reaction returned 0");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_REACTION, "wrong type");

    const nx_msg_field_t *f = nx_msg_find_field(&msg, NX_FIELD_REACTION);
    ASSERT(f != NULL, "reaction field not found");
    ASSERT(f->len == 8, "wrong reaction length");
    ASSERT(memcmp(f->data, "thumbsup", 8) == 0, "reaction mismatch");

    PASS();
}

static void test_nickname_message(void)
{
    TEST("nickname announcement message");

    uint8_t buf[128];
    size_t len = nx_msg_build_nickname(buf, sizeof(buf), "Alice");
    ASSERT(len > 0, "build_nickname returned 0");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_NICKNAME, "wrong type");

    const nx_msg_field_t *f = nx_msg_find_field(&msg, NX_FIELD_NICKNAME);
    ASSERT(f != NULL, "nickname field not found");
    ASSERT(f->len == 5, "wrong nickname length");
    ASSERT(memcmp(f->data, "Alice", 5) == 0, "nickname mismatch");

    PASS();
}

static void test_contact_share(void)
{
    TEST("contact share message");

    nx_addr_short_t addr = {{ 0xDE, 0xAD, 0xBE, 0xEF }};
    uint8_t pubkey[NX_PUBKEY_SIZE];
    memset(pubkey, 0x42, NX_PUBKEY_SIZE);

    uint8_t buf[256];
    size_t len = nx_msg_build_contact(buf, sizeof(buf), &addr, pubkey);
    ASSERT(len > 0, "build_contact returned 0");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_CONTACT, "wrong type");

    const nx_msg_field_t *fa = nx_msg_find_field(&msg, NX_FIELD_CONTACT_ADDR);
    ASSERT(fa != NULL, "contact addr not found");
    ASSERT(fa->len == 4, "wrong addr length");
    ASSERT(memcmp(fa->data, addr.bytes, 4) == 0, "addr mismatch");

    const nx_msg_field_t *fp = nx_msg_find_field(&msg, NX_FIELD_CONTACT_PUB);
    ASSERT(fp != NULL, "contact pubkey not found");
    ASSERT(fp->len == 32, "wrong pubkey length");
    ASSERT(memcmp(fp->data, pubkey, 32) == 0, "pubkey mismatch");

    PASS();
}

/* ── Builder with multiple fields ───────────────────────────────────── */

static void test_builder_multi_field(void)
{
    TEST("builder with custom multi-field message");

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_TEXT, NX_MSG_FLAG_ENCRYPTED);

    nx_msg_id_t mid;
    nx_msg_id_generate(&mid);
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_MSG_ID, mid.bytes, 4) == NX_OK, "add msg_id");
    ASSERT(nx_msg_builder_add_text(&b, "Test message") == NX_OK, "add text");
    ASSERT(nx_msg_builder_add_nickname(&b, "Bob") == NX_OK, "add nickname");

    size_t out_len;
    const uint8_t *data = nx_msg_builder_finish(&b, &out_len);
    ASSERT(data != NULL, "finish returned NULL");
    ASSERT(out_len > 0, "zero length");

    nx_message_t msg;
    ASSERT(nx_msg_parse(data, out_len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.field_count == 3, "wrong field count");
    ASSERT(msg.flags & NX_MSG_FLAG_ENCRYPTED, "encrypted flag not set");

    size_t tlen;
    const char *text = nx_msg_get_text(&msg, &tlen);
    ASSERT(text != NULL, "text not found");
    ASSERT(tlen == 12, "wrong text len");
    ASSERT(memcmp(text, "Test message", 12) == 0, "text mismatch");

    PASS();
}

/* ── Error cases ────────────────────────────────────────────────────── */

static void test_parse_empty_buffer(void)
{
    TEST("parse rejects empty / too-short buffer");

    nx_message_t msg;
    ASSERT(nx_msg_parse(NULL, 0, &msg) == NX_ERR_INVALID_ARG, "NULL not rejected");
    ASSERT(nx_msg_parse((uint8_t[]){0}, 1, &msg) == NX_ERR_INVALID_ARG, "short not rejected");

    PASS();
}

static void test_parse_wrong_version(void)
{
    TEST("parse rejects wrong version");

    uint8_t buf[8] = {99, NX_MSG_TEXT, 0, 0,0,0,0, 0};
    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, sizeof(buf), &msg) == NX_ERR_INVALID_ARG, "bad version accepted");

    PASS();
}

static void test_parse_truncated_field(void)
{
    TEST("parse rejects truncated field data");

    /* Build a valid message header claiming 1 field, but truncate the data */
    uint8_t buf[12] = {NX_MSG_VERSION, NX_MSG_TEXT, 0, 0,0,0,0, 1,
                        NX_FIELD_TEXT, 0xFF, 0x00}; /* claims 255 bytes, only 0 available */
    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, sizeof(buf), &msg) == NX_ERR_INVALID_ARG, "truncated accepted");

    PASS();
}

static void test_builder_overflow(void)
{
    TEST("builder rejects data exceeding max size");

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_FILE, 0);

    /* Try to add a huge field */
    uint8_t big[4000];
    memset(big, 0x42, sizeof(big));
    nx_err_t err = nx_msg_builder_add(&b, NX_FIELD_FILEDATA, big, sizeof(big));
    ASSERT(err == NX_ERR_BUFFER_TOO_SMALL, "overflow not rejected");

    PASS();
}

static void test_msg_id_generation(void)
{
    TEST("message ID generation produces unique IDs");

    nx_msg_id_t a, b;
    nx_msg_id_generate(&a);
    nx_msg_id_generate(&b);

    /* IDs should not be identical (very unlikely unless broken) */
    ASSERT(nx_msg_id_cmp(&a, &b) != 0 ||
           memcmp(a.bytes, b.bytes, 4) == 0, "IDs should differ");

    PASS();
}

static void test_find_field_missing(void)
{
    TEST("find_field returns NULL for missing field");

    uint8_t buf[64];
    size_t len = nx_msg_build_ack(buf, sizeof(buf), &(nx_msg_id_t){{1,2,3,4}});
    ASSERT(len > 0, "build failed");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");

    /* ACK has no text field */
    ASSERT(nx_msg_find_field(&msg, NX_FIELD_TEXT) == NULL, "found non-existent field");
    ASSERT(nx_msg_get_text(&msg, NULL) == NULL, "got text from ACK");

    PASS();
}

static void test_location_negative_coords(void)
{
    TEST("location with negative coordinates (southern/western)");

    uint8_t buf[256];
    size_t len = nx_msg_build_location(buf, sizeof(buf),
                                        -33.8688, 151.2093, -5, 10);
    ASSERT(len > 0, "build failed");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");

    double lat, lon;
    int16_t alt;
    uint8_t acc;
    ASSERT(nx_msg_get_location(&msg, &lat, &lon, &alt, &acc) == NX_OK, "get failed");
    ASSERT(fabs(lat - (-33.8688)) < 0.0001, "lat mismatch");
    ASSERT(fabs(lon - 151.2093) < 0.0001, "lon mismatch");

    PASS();
}

/* File transfer message (image with filename) */
static void test_file_message(void)
{
    TEST("file message with filename + mimetype + data");

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_IMAGE, 0);

    nx_msg_id_t mid;
    nx_msg_id_generate(&mid);
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_MSG_ID, mid.bytes, 4) == NX_OK, "add id");
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_FILENAME,
            (const uint8_t *)"photo.jpg", 9) == NX_OK, "add filename");
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_MIMETYPE,
            (const uint8_t *)"image/jpeg", 10) == NX_OK, "add mime");

    /* Small fake image data */
    uint8_t fake_data[64];
    memset(fake_data, 0xFF, sizeof(fake_data));
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_FILEDATA, fake_data, 64) == NX_OK, "add data");

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    ASSERT(result != NULL && out_len > 0, "finish failed");

    nx_message_t msg;
    ASSERT(nx_msg_parse(result, out_len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_IMAGE, "wrong type");
    ASSERT(msg.field_count == 4, "wrong field count");

    const nx_msg_field_t *fn = nx_msg_find_field(&msg, NX_FIELD_FILENAME);
    ASSERT(fn && fn->len == 9, "filename");
    ASSERT(memcmp(fn->data, "photo.jpg", 9) == 0, "filename mismatch");

    const nx_msg_field_t *fd = nx_msg_find_field(&msg, NX_FIELD_FILEDATA);
    ASSERT(fd && fd->len == 64, "filedata");

    PASS();
}

/* ── Read receipt ────────────────────────────────────────────────────── */

static void test_read_receipt(void)
{
    TEST("read receipt build + parse roundtrip");

    nx_msg_id_t mid;
    nx_msg_id_generate(&mid);

    uint8_t buf[64];
    size_t len = nx_msg_build_read(buf, sizeof(buf), &mid);
    ASSERT(len > 0, "build_read returned 0");
    ASSERT(len < 20, "read receipt too large");

    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.type == NX_MSG_READ, "wrong type");

    nx_msg_id_t parsed_id;
    ASSERT(nx_msg_get_msg_id(&msg, &parsed_id) == NX_OK, "msg_id not found");
    ASSERT(nx_msg_id_cmp(&mid, &parsed_id) == 0, "msg_id mismatch");

    PASS();
}

/* ── Propagation flag ───────────────────────────────────────────────── */

static void test_propagate_flag_roundtrip(void)
{
    TEST("propagate flag roundtrip");

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_TEXT, NX_MSG_FLAG_PROPAGATE);
    nx_msg_builder_add_text(&b, "store-and-forward me");

    size_t out_len;
    const uint8_t *data = nx_msg_builder_finish(&b, &out_len);
    ASSERT(data != NULL, "finish returned NULL");

    nx_message_t msg;
    ASSERT(nx_msg_parse(data, out_len, &msg) == NX_OK, "parse failed");
    ASSERT(msg.flags & NX_MSG_FLAG_PROPAGATE, "propagate flag not set");

    PASS();
}

/* ── Signatures ─────────────────────────────────────────────────────── */

static void test_sign_verify_roundtrip(void)
{
    TEST("sign + verify roundtrip");

    nx_identity_t id;
    nx_identity_generate(&id);

    uint8_t buf[512];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Sign me!", NULL);
    ASSERT(len > 0, "build_text returned 0");

    size_t signed_len = nx_msg_sign(buf, len, sizeof(buf), id.sign_secret);
    ASSERT(signed_len > 0, "sign returned 0");
    ASSERT(signed_len == len + NX_MSG_SIGNATURE_OVERHEAD, "wrong signed length");

    /* Verify SIGNED flag is set */
    ASSERT(buf[2] & NX_MSG_FLAG_SIGNED, "SIGNED flag not set");

    /* Parse still works */
    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, signed_len, &msg) == NX_OK, "parse signed failed");
    ASSERT(msg.flags & NX_MSG_FLAG_SIGNED, "parsed SIGNED flag missing");

    /* Signature field is last field */
    ASSERT(msg.fields[msg.field_count - 1].type == NX_FIELD_SIGNATURE,
           "last field not signature");
    ASSERT(msg.fields[msg.field_count - 1].len == 64, "sig field wrong len");

    /* Verify passes */
    nx_err_t err = nx_msg_verify(buf, signed_len, id.sign_public);
    ASSERT(err == NX_OK, "verify failed on valid signature");

    PASS();
}

static void test_verify_tampered(void)
{
    TEST("verify rejects tampered message");

    nx_identity_t id;
    nx_identity_generate(&id);

    uint8_t buf[512];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Tamper test", NULL);
    size_t signed_len = nx_msg_sign(buf, len, sizeof(buf), id.sign_secret);
    ASSERT(signed_len > 0, "sign failed");

    /* Tamper with a body byte */
    buf[NX_MSG_HEADER_SIZE + 5] ^= 0xFF;

    nx_err_t err = nx_msg_verify(buf, signed_len, id.sign_public);
    ASSERT(err == NX_ERR_AUTH_FAIL, "tampered message verified");

    PASS();
}

static void test_verify_wrong_key(void)
{
    TEST("verify rejects wrong public key");

    nx_identity_t id1, id2;
    nx_identity_generate(&id1);
    nx_identity_generate(&id2);

    uint8_t buf[512];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Wrong key test", NULL);
    size_t signed_len = nx_msg_sign(buf, len, sizeof(buf), id1.sign_secret);
    ASSERT(signed_len > 0, "sign failed");

    /* Verify with wrong key */
    nx_err_t err = nx_msg_verify(buf, signed_len, id2.sign_public);
    ASSERT(err == NX_ERR_AUTH_FAIL, "wrong key accepted");

    PASS();
}

static void test_unsigned_verify(void)
{
    TEST("verify rejects unsigned message");

    nx_identity_t id;
    nx_identity_generate(&id);

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Not signed", NULL);

    nx_err_t err = nx_msg_verify(buf, len, id.sign_public);
    ASSERT(err == NX_ERR_INVALID_ARG, "unsigned message not rejected");

    PASS();
}

static void test_sign_buffer_too_small(void)
{
    TEST("sign rejects insufficient buffer space");

    nx_identity_t id;
    nx_identity_generate(&id);

    uint8_t buf[64]; /* Too small for message + signature */
    size_t len = nx_msg_build_text(buf, sizeof(buf), "Tiny buf", NULL);
    ASSERT(len > 0, "build failed");

    /* Buffer has no room for 67-byte signature overhead */
    size_t signed_len = nx_msg_sign(buf, len, len, id.sign_secret);
    ASSERT(signed_len == 0, "sign should fail with no spare space");

    PASS();
}

static void test_title_field(void)
{
    TEST("title field round-trip (LXMF parity)");

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_TEXT, 0);

    nx_msg_id_t id;
    nx_msg_id_generate(&id);
    ASSERT(nx_msg_builder_add(&b, NX_FIELD_MSG_ID, id.bytes, 4) == NX_OK, "add msg_id");
    ASSERT(nx_msg_builder_add_title(&b, "Re: meeting") == NX_OK, "add title");
    ASSERT(nx_msg_builder_add_text(&b, "see you at 10") == NX_OK, "add text");

    size_t out_len;
    const uint8_t *out = nx_msg_builder_finish(&b, &out_len);
    ASSERT(out != NULL && out_len > 0, "finish");

    nx_message_t msg;
    ASSERT(nx_msg_parse(out, out_len, &msg) == NX_OK, "parse");
    const nx_msg_field_t *t = nx_msg_find_field(&msg, NX_FIELD_TITLE);
    ASSERT(t != NULL, "title field missing");
    ASSERT(t->len == 11, "title length");
    ASSERT(memcmp(t->data, "Re: meeting", 11) == 0, "title bytes");

    PASS();
}

static void test_stamp_roundtrip(void)
{
    TEST("PoW stamp round-trip (low difficulty)");

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "stamp me", NULL);
    ASSERT(len > 0, "build");

    size_t stamped = nx_msg_stamp(buf, len, sizeof(buf), 8, 1000000ULL);
    ASSERT(stamped == len + NX_MSG_STAMP_OVERHEAD, "stamp length");
    ASSERT(buf[2] & NX_MSG_FLAG_STAMPED, "STAMPED flag set");

    nx_err_t err = nx_msg_verify_stamp(buf, stamped, 8);
    ASSERT(err == NX_OK, "verify accepted");

    /* parse should still see the stamp field */
    nx_message_t msg;
    ASSERT(nx_msg_parse(buf, stamped, &msg) == NX_OK, "parse");
    const nx_msg_field_t *s = nx_msg_find_field(&msg, NX_FIELD_STAMP);
    ASSERT(s != NULL && s->len == NX_MSG_STAMP_VALUE_SIZE, "stamp field");

    PASS();
}

static void test_stamp_min_difficulty_rejects(void)
{
    TEST("verify rejects stamp below requested difficulty");

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "weak stamp", NULL);
    size_t stamped = nx_msg_stamp(buf, len, sizeof(buf), 4, 1000000ULL);
    ASSERT(stamped > 0, "stamp");

    /* Asking for a higher minimum than the stamp carries should fail. */
    nx_err_t err = nx_msg_verify_stamp(buf, stamped, 16);
    ASSERT(err == NX_ERR_AUTH_FAIL, "min difficulty enforced");
    PASS();
}

static void test_stamp_unstamped_rejected(void)
{
    TEST("verify rejects unstamped message");

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "no stamp", NULL);
    nx_err_t err = nx_msg_verify_stamp(buf, len, 1);
    ASSERT(err == NX_ERR_INVALID_ARG, "unstamped rejected");
    PASS();
}

static void test_stamp_tampered_rejected(void)
{
    TEST("verify rejects tampered stamped message");

    uint8_t buf[256];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "tamper", NULL);
    size_t stamped = nx_msg_stamp(buf, len, sizeof(buf), 8, 1000000ULL);
    ASSERT(stamped > 0, "stamp");

    /* Flip a payload byte after the header but before the stamp field. */
    buf[NX_MSG_HEADER_SIZE + 4] ^= 0x01;

    nx_err_t err = nx_msg_verify_stamp(buf, stamped, 8);
    ASSERT(err == NX_ERR_AUTH_FAIL, "tamper detected");
    PASS();
}

static void test_stamp_buffer_too_small(void)
{
    TEST("stamp rejects insufficient buffer");

    uint8_t buf[64];
    size_t len = nx_msg_build_text(buf, sizeof(buf), "short", NULL);
    /* No spare room for stamp overhead. */
    size_t stamped = nx_msg_stamp(buf, len, len, 4, 100);
    ASSERT(stamped == 0, "no-room rejected");
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(void)
{
    printf("NEXUS Message Format Tests\n");
    printf("==========================\n\n");

    test_text_message_roundtrip();
    test_text_with_reply();
    test_location_message();
    test_ack_message();
    test_reaction_message();
    test_nickname_message();
    test_contact_share();
    test_builder_multi_field();
    test_parse_empty_buffer();
    test_parse_wrong_version();
    test_parse_truncated_field();
    test_builder_overflow();
    test_msg_id_generation();
    test_find_field_missing();
    test_location_negative_coords();
    test_file_message();
    /* Phase 9a tests */
    test_read_receipt();
    test_propagate_flag_roundtrip();
    test_sign_verify_roundtrip();
    test_verify_tampered();
    test_verify_wrong_key();
    test_unsigned_verify();
    test_sign_buffer_too_small();
    test_title_field();
    test_stamp_roundtrip();
    test_stamp_min_difficulty_rejects();
    test_stamp_unstamped_rejected();
    test_stamp_tampered_rejected();
    test_stamp_buffer_too_small();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
