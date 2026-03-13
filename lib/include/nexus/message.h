/*
 * NEXUS Protocol -- Structured Message Format (NXM)
 *
 * Provides a structured message envelope similar to Reticulum's LXMF,
 * enabling rich messaging (text, files, location, reactions, voice notes)
 * over the NEXUS mesh network.
 *
 * Wire format (little-endian):
 *   [version(1)][type(1)][flags(1)][timestamp(4)][field_count(1)][fields...]
 *
 * Each field:
 *   [field_type(1)][field_len(2)][field_data(field_len)]
 *
 * Total header overhead: 8 bytes + per-field overhead (3 bytes each)
 * Fits within single NEXUS packet (242B payload) for small messages,
 * or uses fragmentation (up to 3808B) for files/images.
 *
 * This layer is ABOVE the NEXUS transport -- messages are serialized
 * into byte buffers and sent via nx_node_send_session() or nx_node_send().
 */
#ifndef NEXUS_MESSAGE_H
#define NEXUS_MESSAGE_H

#include "types.h"

/* ── Message format version ─────────────────────────────────────────── */
#define NX_MSG_VERSION       1

/* ── Message types ──────────────────────────────────────────────────── */
typedef enum {
    NX_MSG_TEXT        = 0x01,  /* Plain text message */
    NX_MSG_FILE        = 0x02,  /* File transfer */
    NX_MSG_IMAGE       = 0x03,  /* Image (JPEG/PNG data) */
    NX_MSG_LOCATION    = 0x04,  /* GPS location share */
    NX_MSG_VOICE_NOTE  = 0x05,  /* Voice note (Codec2/Opus data) */
    NX_MSG_REACTION    = 0x06,  /* Reaction to a previous message */
    NX_MSG_ACK         = 0x07,  /* Message delivery acknowledgement */
    NX_MSG_TYPING      = 0x08,  /* Typing indicator */
    NX_MSG_READ        = 0x09,  /* Read receipt */
    NX_MSG_DELETE      = 0x0A,  /* Delete request */
    NX_MSG_NICKNAME    = 0x0B,  /* Set display name */
    NX_MSG_CONTACT     = 0x0C,  /* Share a contact (address + pubkey) */
} nx_msg_type_t;

/* ── Message flags (bitfield) ───────────────────────────────────────── */
#define NX_MSG_FLAG_ENCRYPTED   0x01  /* Payload is E2E encrypted */
#define NX_MSG_FLAG_SIGNED      0x02  /* Message is signed */
#define NX_MSG_FLAG_PROPAGATE   0x04  /* Request store-and-forward */
#define NX_MSG_FLAG_URGENT      0x08  /* High priority */
#define NX_MSG_FLAG_REPLY       0x10  /* Contains reply-to field */
#define NX_MSG_FLAG_GROUP       0x20  /* Group message */

/* ── Field types (TLV fields within message) ────────────────────────── */
typedef enum {
    NX_FIELD_TEXT        = 0x01,  /* UTF-8 text content */
    NX_FIELD_FILENAME    = 0x02,  /* Filename for file/image transfer */
    NX_FIELD_MIMETYPE    = 0x03,  /* MIME type string */
    NX_FIELD_FILEDATA    = 0x04,  /* Raw file/image data */
    NX_FIELD_LATITUDE    = 0x05,  /* int32: lat * 1e7 */
    NX_FIELD_LONGITUDE   = 0x06,  /* int32: lon * 1e7 */
    NX_FIELD_ALTITUDE    = 0x07,  /* int16: meters */
    NX_FIELD_ACCURACY    = 0x08,  /* uint8: meters */
    NX_FIELD_REPLY_TO    = 0x09,  /* 4-byte message ID being replied to */
    NX_FIELD_REACTION    = 0x0A,  /* UTF-8 reaction emoji/text */
    NX_FIELD_MSG_ID      = 0x0B,  /* 4-byte message ID (target for ack/react/delete) */
    NX_FIELD_NICKNAME    = 0x0C,  /* UTF-8 display name */
    NX_FIELD_DURATION    = 0x0D,  /* uint16: duration in seconds (voice notes) */
    NX_FIELD_THUMBNAIL   = 0x0E,  /* Small preview image data */
    NX_FIELD_CONTACT_ADDR = 0x0F, /* 4-byte NEXUS short address */
    NX_FIELD_CONTACT_PUB  = 0x10, /* 32-byte Ed25519 pubkey */
    NX_FIELD_CODEC       = 0x11,  /* uint8: audio codec ID */
    NX_FIELD_SIGNATURE   = 0x12,  /* 64-byte Ed25519 signature */
} nx_field_type_t;

/* ── Size limits ────────────────────────────────────────────────────── */
#define NX_MSG_HEADER_SIZE     8     /* version + type + flags + timestamp + field_count */
#define NX_MSG_FIELD_HEADER    3     /* type(1) + len(2) */
#define NX_MSG_MAX_FIELDS     16     /* Max fields per message */
#define NX_MSG_MAX_TEXT       200     /* Max text per single-packet message */
#define NX_MSG_MAX_NICKNAME    32     /* Max nickname length */
#define NX_MSG_MAX_SIZE      3800     /* Max serialized message (fits in fragmentation) */

/* ── Message ID ─────────────────────────────────────────────────────── */
/* 4-byte message ID: [timestamp_lo(2)][random(2)] */
typedef struct {
    uint8_t bytes[4];
} nx_msg_id_t;

/* ── Field structure ────────────────────────────────────────────────── */
typedef struct {
    nx_field_type_t type;
    uint16_t        len;
    const uint8_t  *data;       /* Points into serialized buffer (not owned) */
} nx_msg_field_t;

/* ── Parsed message ─────────────────────────────────────────────────── */
typedef struct {
    uint8_t         version;
    nx_msg_type_t   type;
    uint8_t         flags;
    uint32_t        timestamp;  /* Unix epoch seconds (truncated to 32 bits) */
    uint8_t         field_count;
    nx_msg_field_t  fields[NX_MSG_MAX_FIELDS];
} nx_message_t;

/* ── Builder (for constructing messages) ────────────────────────────── */
typedef struct {
    uint8_t         buf[NX_MSG_MAX_SIZE];
    size_t          len;        /* Current write position */
    nx_msg_type_t   type;
    uint8_t         flags;
    uint8_t         field_count;
} nx_msg_builder_t;

/* ── API: Builder ───────────────────────────────────────────────────── */

/* Initialize a message builder for a given type. */
void nx_msg_builder_init(nx_msg_builder_t *b, nx_msg_type_t type, uint8_t flags);

/* Add a field to the message. Returns NX_ERR_FULL if no space. */
nx_err_t nx_msg_builder_add(nx_msg_builder_t *b,
                             nx_field_type_t field_type,
                             const uint8_t *data, uint16_t len);

/* Convenience: add a text field (UTF-8 string, no null terminator stored). */
nx_err_t nx_msg_builder_add_text(nx_msg_builder_t *b, const char *text);

/* Convenience: add a location (lat/lon as doubles, converted to int32 * 1e7). */
nx_err_t nx_msg_builder_add_location(nx_msg_builder_t *b,
                                      double lat, double lon,
                                      int16_t alt_m, uint8_t accuracy_m);

/* Convenience: add a reply-to reference. */
nx_err_t nx_msg_builder_add_reply(nx_msg_builder_t *b, const nx_msg_id_t *reply_to);

/* Convenience: add a nickname. */
nx_err_t nx_msg_builder_add_nickname(nx_msg_builder_t *b, const char *name);

/* Finalize and get the serialized buffer. Writes the header.
 * Returns pointer to internal buffer and sets *out_len. */
const uint8_t *nx_msg_builder_finish(nx_msg_builder_t *b, size_t *out_len);

/* ── API: Parser ────────────────────────────────────────────────────── */

/* Parse a serialized message buffer into an nx_message_t.
 * Field data pointers point into the input buffer (zero-copy).
 * Returns NX_OK on success, NX_ERR_INVALID_ARG on malformed data. */
nx_err_t nx_msg_parse(const uint8_t *data, size_t len, nx_message_t *msg);

/* Find a field by type in a parsed message. Returns NULL if not found. */
const nx_msg_field_t *nx_msg_find_field(const nx_message_t *msg,
                                         nx_field_type_t type);

/* Get text content from a message (returns pointer + length, not null-terminated). */
const char *nx_msg_get_text(const nx_message_t *msg, size_t *out_len);

/* Get location fields from a message. Returns NX_ERR_NOT_FOUND if absent. */
nx_err_t nx_msg_get_location(const nx_message_t *msg,
                              double *lat, double *lon,
                              int16_t *alt_m, uint8_t *accuracy_m);

/* Get the message ID field. Returns NX_ERR_NOT_FOUND if absent. */
nx_err_t nx_msg_get_msg_id(const nx_message_t *msg, nx_msg_id_t *id);

/* ── API: Helpers ───────────────────────────────────────────────────── */

/* Generate a unique message ID (timestamp + random). */
void nx_msg_id_generate(nx_msg_id_t *id);

/* Compare two message IDs. Returns 0 if equal. */
int nx_msg_id_cmp(const nx_msg_id_t *a, const nx_msg_id_t *b);

/* ── High-level: Build common message types ─────────────────────────── */

/* Build a text message. Returns serialized length, or 0 on error. */
size_t nx_msg_build_text(uint8_t *buf, size_t buf_len,
                          const char *text, const nx_msg_id_t *reply_to);

/* Build a delivery ACK. Returns serialized length. */
size_t nx_msg_build_ack(uint8_t *buf, size_t buf_len,
                         const nx_msg_id_t *ack_id);

/* Build a reaction. Returns serialized length. */
size_t nx_msg_build_reaction(uint8_t *buf, size_t buf_len,
                              const nx_msg_id_t *target_id,
                              const char *reaction);

/* Build a location share. Returns serialized length. */
size_t nx_msg_build_location(uint8_t *buf, size_t buf_len,
                              double lat, double lon,
                              int16_t alt_m, uint8_t accuracy_m);

/* Build a nickname announcement. Returns serialized length. */
size_t nx_msg_build_nickname(uint8_t *buf, size_t buf_len,
                              const char *nickname);

/* Build a contact share. Returns serialized length. */
size_t nx_msg_build_contact(uint8_t *buf, size_t buf_len,
                             const nx_addr_short_t *addr,
                             const uint8_t pubkey[NX_PUBKEY_SIZE]);

/* Build a read receipt. Returns serialized length. */
size_t nx_msg_build_read(uint8_t *buf, size_t buf_len,
                          const nx_msg_id_t *msg_id);

/* ── API: Signatures ────────────────────────────────────────────────── */

/* Signature field overhead: type(1) + len(2) + sig(64) = 67 bytes */
#define NX_MSG_SIGNATURE_OVERHEAD  (NX_MSG_FIELD_HEADER + NX_SIGNATURE_SIZE)

/*
 * Append an Ed25519 signature to a serialized NXM message.
 * Signs buf[0..len) and appends a SIGNATURE field.
 * Sets NX_MSG_FLAG_SIGNED and increments field_count.
 * Returns new total length, or 0 on error.
 */
size_t nx_msg_sign(uint8_t *buf, size_t len, size_t buf_cap,
                   const uint8_t sign_secret[NX_SIGN_SECRET_SIZE]);

/*
 * Verify Ed25519 signature on a signed NXM message.
 * Returns NX_OK if valid, NX_ERR_AUTH_FAIL if invalid,
 * NX_ERR_INVALID_ARG if not signed or malformed.
 */
nx_err_t nx_msg_verify(const uint8_t *buf, size_t len,
                       const uint8_t sign_pubkey[NX_PUBKEY_SIZE]);

#endif /* NEXUS_MESSAGE_H */
