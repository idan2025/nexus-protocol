/*
 * NEXUS Protocol -- Structured Message Format (NXM) implementation
 */
#define _POSIX_C_SOURCE 199309L
#include "nexus/message.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"
#include <string.h>

/* ── Little-endian helpers ──────────────────────────────────────────── */

static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t read_i32(const uint8_t *p)
{
    uint32_t v = read_u32(p);
    return (int32_t)v;
}

/* ── Builder ────────────────────────────────────────────────────────── */

void nx_msg_builder_init(nx_msg_builder_t *b, nx_msg_type_t type, uint8_t flags)
{
    memset(b, 0, sizeof(*b));
    b->type = type;
    b->flags = flags;
    b->len = NX_MSG_HEADER_SIZE; /* Reserve space for header */
}

nx_err_t nx_msg_builder_add(nx_msg_builder_t *b,
                             nx_field_type_t field_type,
                             const uint8_t *data, uint16_t len)
{
    if (b->field_count >= NX_MSG_MAX_FIELDS)
        return NX_ERR_FULL;
    if (b->len + NX_MSG_FIELD_HEADER + len > NX_MSG_MAX_SIZE)
        return NX_ERR_BUFFER_TOO_SMALL;

    /* Write field header: type(1) + len(2) */
    b->buf[b->len++] = (uint8_t)field_type;
    write_u16(&b->buf[b->len], len);
    b->len += 2;

    /* Write field data */
    if (len > 0 && data) {
        memcpy(&b->buf[b->len], data, len);
        b->len += len;
    }

    b->field_count++;
    return NX_OK;
}

nx_err_t nx_msg_builder_add_text(nx_msg_builder_t *b, const char *text)
{
    if (!text) return NX_ERR_INVALID_ARG;
    size_t slen = strlen(text);
    if (slen > 0xFFFF) return NX_ERR_BUFFER_TOO_SMALL;
    return nx_msg_builder_add(b, NX_FIELD_TEXT, (const uint8_t *)text, (uint16_t)slen);
}

nx_err_t nx_msg_builder_add_location(nx_msg_builder_t *b,
                                      double lat, double lon,
                                      int16_t alt_m, uint8_t accuracy_m)
{
    nx_err_t err;
    uint8_t lat_buf[4], lon_buf[4], alt_buf[2];

    /* Encode lat/lon as int32 * 1e7 (same as GPS integer format) */
    int32_t ilat = (int32_t)(lat * 1e7);
    int32_t ilon = (int32_t)(lon * 1e7);

    write_u32(lat_buf, (uint32_t)ilat);
    err = nx_msg_builder_add(b, NX_FIELD_LATITUDE, lat_buf, 4);
    if (err != NX_OK) return err;

    write_u32(lon_buf, (uint32_t)ilon);
    err = nx_msg_builder_add(b, NX_FIELD_LONGITUDE, lon_buf, 4);
    if (err != NX_OK) return err;

    write_u16(alt_buf, (uint16_t)alt_m);
    err = nx_msg_builder_add(b, NX_FIELD_ALTITUDE, alt_buf, 2);
    if (err != NX_OK) return err;

    err = nx_msg_builder_add(b, NX_FIELD_ACCURACY, &accuracy_m, 1);
    return err;
}

nx_err_t nx_msg_builder_add_reply(nx_msg_builder_t *b, const nx_msg_id_t *reply_to)
{
    if (!reply_to) return NX_ERR_INVALID_ARG;
    b->flags |= NX_MSG_FLAG_REPLY;
    return nx_msg_builder_add(b, NX_FIELD_REPLY_TO, reply_to->bytes, 4);
}

nx_err_t nx_msg_builder_add_nickname(nx_msg_builder_t *b, const char *name)
{
    if (!name) return NX_ERR_INVALID_ARG;
    size_t slen = strlen(name);
    if (slen > NX_MSG_MAX_NICKNAME) slen = NX_MSG_MAX_NICKNAME;
    return nx_msg_builder_add(b, NX_FIELD_NICKNAME, (const uint8_t *)name, (uint16_t)slen);
}

const uint8_t *nx_msg_builder_finish(nx_msg_builder_t *b, size_t *out_len)
{
    /* Write header at the beginning */
    b->buf[0] = NX_MSG_VERSION;
    b->buf[1] = (uint8_t)b->type;
    b->buf[2] = b->flags;
    /* Timestamp: seconds since epoch, truncated to 32 bits */
    uint32_t ts = (uint32_t)(nx_platform_time_ms() / 1000);
    write_u32(&b->buf[3], ts);
    b->buf[7] = b->field_count;

    if (out_len) *out_len = b->len;
    return b->buf;
}

/* ── Parser ─────────────────────────────────────────────────────────── */

nx_err_t nx_msg_parse(const uint8_t *data, size_t len, nx_message_t *msg)
{
    if (!data || !msg) return NX_ERR_INVALID_ARG;
    if (len < NX_MSG_HEADER_SIZE) return NX_ERR_INVALID_ARG;

    memset(msg, 0, sizeof(*msg));

    msg->version     = data[0];
    msg->type        = (nx_msg_type_t)data[1];
    msg->flags       = data[2];
    msg->timestamp   = read_u32(&data[3]);
    msg->field_count = data[7];

    if (msg->version != NX_MSG_VERSION) return NX_ERR_INVALID_ARG;
    if (msg->field_count > NX_MSG_MAX_FIELDS) return NX_ERR_INVALID_ARG;

    /* Parse fields */
    size_t pos = NX_MSG_HEADER_SIZE;
    for (uint8_t i = 0; i < msg->field_count; i++) {
        if (pos + NX_MSG_FIELD_HEADER > len) return NX_ERR_INVALID_ARG;

        msg->fields[i].type = (nx_field_type_t)data[pos];
        msg->fields[i].len  = read_u16(&data[pos + 1]);
        pos += NX_MSG_FIELD_HEADER;

        if (pos + msg->fields[i].len > len) return NX_ERR_INVALID_ARG;

        msg->fields[i].data = &data[pos];
        pos += msg->fields[i].len;
    }

    return NX_OK;
}

const nx_msg_field_t *nx_msg_find_field(const nx_message_t *msg,
                                         nx_field_type_t type)
{
    if (!msg) return NULL;
    for (uint8_t i = 0; i < msg->field_count; i++) {
        if (msg->fields[i].type == type) return &msg->fields[i];
    }
    return NULL;
}

const char *nx_msg_get_text(const nx_message_t *msg, size_t *out_len)
{
    const nx_msg_field_t *f = nx_msg_find_field(msg, NX_FIELD_TEXT);
    if (!f) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = f->len;
    return (const char *)f->data;
}

nx_err_t nx_msg_get_location(const nx_message_t *msg,
                              double *lat, double *lon,
                              int16_t *alt_m, uint8_t *accuracy_m)
{
    const nx_msg_field_t *flat = nx_msg_find_field(msg, NX_FIELD_LATITUDE);
    const nx_msg_field_t *flon = nx_msg_find_field(msg, NX_FIELD_LONGITUDE);
    if (!flat || flat->len < 4 || !flon || flon->len < 4)
        return NX_ERR_NOT_FOUND;

    if (lat) *lat = (double)read_i32(flat->data) / 1e7;
    if (lon) *lon = (double)read_i32(flon->data) / 1e7;

    const nx_msg_field_t *falt = nx_msg_find_field(msg, NX_FIELD_ALTITUDE);
    if (alt_m) {
        *alt_m = (falt && falt->len >= 2) ? (int16_t)read_u16(falt->data) : 0;
    }

    const nx_msg_field_t *facc = nx_msg_find_field(msg, NX_FIELD_ACCURACY);
    if (accuracy_m) {
        *accuracy_m = (facc && facc->len >= 1) ? facc->data[0] : 0;
    }

    return NX_OK;
}

nx_err_t nx_msg_get_msg_id(const nx_message_t *msg, nx_msg_id_t *id)
{
    const nx_msg_field_t *f = nx_msg_find_field(msg, NX_FIELD_MSG_ID);
    if (!f || f->len < 4) return NX_ERR_NOT_FOUND;
    memcpy(id->bytes, f->data, 4);
    return NX_OK;
}

/* ── Helpers ────────────────────────────────────────────────────────── */

void nx_msg_id_generate(nx_msg_id_t *id)
{
    uint32_t ts = (uint32_t)(nx_platform_time_ms() / 1000);
    id->bytes[0] = (uint8_t)(ts);
    id->bytes[1] = (uint8_t)(ts >> 8);
    /* Random bytes for uniqueness */
    uint8_t rnd[2];
    nx_platform_random(rnd, 2);
    id->bytes[2] = rnd[0];
    id->bytes[3] = rnd[1];
}

int nx_msg_id_cmp(const nx_msg_id_t *a, const nx_msg_id_t *b)
{
    return memcmp(a->bytes, b->bytes, 4);
}

/* ── High-level builders ────────────────────────────────────────────── */

size_t nx_msg_build_text(uint8_t *buf, size_t buf_len,
                          const char *text, const nx_msg_id_t *reply_to)
{
    nx_msg_builder_t b;
    uint8_t flags = 0;
    if (reply_to) flags |= NX_MSG_FLAG_REPLY;

    nx_msg_builder_init(&b, NX_MSG_TEXT, flags);

    /* Add message ID */
    nx_msg_id_t mid;
    nx_msg_id_generate(&mid);
    nx_msg_builder_add(&b, NX_FIELD_MSG_ID, mid.bytes, 4);

    /* Add text content */
    if (nx_msg_builder_add_text(&b, text) != NX_OK) return 0;

    /* Add reply reference if provided */
    if (reply_to) {
        nx_msg_builder_add_reply(&b, reply_to);
    }

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_ack(uint8_t *buf, size_t buf_len,
                         const nx_msg_id_t *ack_id)
{
    if (!ack_id) return 0;

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_ACK, 0);
    nx_msg_builder_add(&b, NX_FIELD_MSG_ID, ack_id->bytes, 4);

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_reaction(uint8_t *buf, size_t buf_len,
                              const nx_msg_id_t *target_id,
                              const char *reaction)
{
    if (!target_id || !reaction) return 0;

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_REACTION, 0);
    nx_msg_builder_add(&b, NX_FIELD_MSG_ID, target_id->bytes, 4);

    size_t rlen = strlen(reaction);
    if (rlen > 64) rlen = 64;
    nx_msg_builder_add(&b, NX_FIELD_REACTION, (const uint8_t *)reaction, (uint16_t)rlen);

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_location(uint8_t *buf, size_t buf_len,
                              double lat, double lon,
                              int16_t alt_m, uint8_t accuracy_m)
{
    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_LOCATION, 0);

    nx_msg_id_t mid;
    nx_msg_id_generate(&mid);
    nx_msg_builder_add(&b, NX_FIELD_MSG_ID, mid.bytes, 4);

    if (nx_msg_builder_add_location(&b, lat, lon, alt_m, accuracy_m) != NX_OK)
        return 0;

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_nickname(uint8_t *buf, size_t buf_len,
                              const char *nickname)
{
    if (!nickname) return 0;

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_NICKNAME, 0);
    nx_msg_builder_add_nickname(&b, nickname);

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_contact(uint8_t *buf, size_t buf_len,
                             const nx_addr_short_t *addr,
                             const uint8_t pubkey[NX_PUBKEY_SIZE])
{
    if (!addr || !pubkey) return 0;

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_CONTACT, 0);
    nx_msg_builder_add(&b, NX_FIELD_CONTACT_ADDR, addr->bytes, 4);
    nx_msg_builder_add(&b, NX_FIELD_CONTACT_PUB, pubkey, NX_PUBKEY_SIZE);

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

size_t nx_msg_build_read(uint8_t *buf, size_t buf_len,
                          const nx_msg_id_t *msg_id)
{
    if (!msg_id) return 0;

    nx_msg_builder_t b;
    nx_msg_builder_init(&b, NX_MSG_READ, 0);
    nx_msg_builder_add(&b, NX_FIELD_MSG_ID, msg_id->bytes, 4);

    size_t out_len;
    const uint8_t *result = nx_msg_builder_finish(&b, &out_len);
    if (out_len > buf_len) return 0;
    memcpy(buf, result, out_len);
    return out_len;
}

/* ── Signatures ─────────────────────────────────────────────────────── */

size_t nx_msg_sign(uint8_t *buf, size_t len, size_t buf_cap,
                   const uint8_t sign_secret[NX_SIGN_SECRET_SIZE])
{
    if (!buf || !sign_secret) return 0;
    if (len < NX_MSG_HEADER_SIZE) return 0;
    if (len + NX_MSG_SIGNATURE_OVERHEAD > buf_cap) return 0;
    if (buf[7] >= NX_MSG_MAX_FIELDS) return 0; /* field_count full */

    /* Sign the message as-is (buf[0..len)) */
    uint8_t sig[NX_SIGNATURE_SIZE];
    crypto_eddsa_sign(sig, sign_secret, buf, len);

    /* Append signature TLV: [type(1)][len_lo(1)][len_hi(1)][sig(64)] */
    size_t pos = len;
    buf[pos++] = (uint8_t)NX_FIELD_SIGNATURE;
    write_u16(&buf[pos], NX_SIGNATURE_SIZE);
    pos += 2;
    memcpy(&buf[pos], sig, NX_SIGNATURE_SIZE);
    pos += NX_SIGNATURE_SIZE;

    /* Update header: set SIGNED flag, increment field_count */
    buf[2] |= NX_MSG_FLAG_SIGNED;
    buf[7]++;

    return pos;
}

nx_err_t nx_msg_verify(const uint8_t *buf, size_t len,
                       const uint8_t sign_pubkey[NX_PUBKEY_SIZE])
{
    if (!buf || !sign_pubkey) return NX_ERR_INVALID_ARG;
    if (len < NX_MSG_HEADER_SIZE + NX_MSG_SIGNATURE_OVERHEAD)
        return NX_ERR_INVALID_ARG;
    if (!(buf[2] & NX_MSG_FLAG_SIGNED))
        return NX_ERR_INVALID_ARG;

    /* Signature field is the last 67 bytes: [type(1)][len(2)][sig(64)] */
    size_t sig_start = len - NX_MSG_SIGNATURE_OVERHEAD;
    if (buf[sig_start] != (uint8_t)NX_FIELD_SIGNATURE)
        return NX_ERR_INVALID_ARG;
    uint16_t sig_len = read_u16(&buf[sig_start + 1]);
    if (sig_len != NX_SIGNATURE_SIZE)
        return NX_ERR_INVALID_ARG;

    const uint8_t *sig = &buf[sig_start + NX_MSG_FIELD_HEADER];

    /* Create scratch header: clear SIGNED flag, decrement field_count */
    uint8_t scratch_hdr[NX_MSG_HEADER_SIZE];
    memcpy(scratch_hdr, buf, NX_MSG_HEADER_SIZE);
    scratch_hdr[2] &= (uint8_t)~NX_MSG_FLAG_SIGNED;
    scratch_hdr[7]--;

    /* The signed content is: scratch_hdr + original body (without sig field) */
    size_t body_len = sig_start - NX_MSG_HEADER_SIZE;

    /* We need to verify the signature over [scratch_hdr || body].
     * Build contiguous buffer for verification. */
    uint8_t verify_buf[NX_MSG_MAX_SIZE];
    size_t verify_len = NX_MSG_HEADER_SIZE + body_len;
    if (verify_len > sizeof(verify_buf))
        return NX_ERR_BUFFER_TOO_SMALL;

    memcpy(verify_buf, scratch_hdr, NX_MSG_HEADER_SIZE);
    memcpy(verify_buf + NX_MSG_HEADER_SIZE, buf + NX_MSG_HEADER_SIZE, body_len);

    int ret = crypto_eddsa_check(sig, sign_pubkey, verify_buf, verify_len);
    return (ret == 0) ? NX_OK : NX_ERR_AUTH_FAIL;
}
