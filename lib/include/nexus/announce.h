/*
 * NEXUS Protocol -- Identity Announcements
 *
 * Announcements let nodes discover each other. Each announcement is
 * Ed25519-signed by the sender so receivers can verify authenticity and
 * derive addresses from the included public key.
 *
 * Wire payload (inside a PTYPE_ANNOUNCE packet):
 *   Base (no telemetry):
 *     [sign_pubkey(32)][x25519_pubkey(32)][role(1)][flags(1)][signature(64)]
 *     Total: 130 bytes. Signature covers the first 66 bytes.
 *
 *   With NX_ANNOUNCE_FLAG_TELEMETRY bit set in flags:
 *     [sign_pubkey(32)][x25519_pubkey(32)][role(1)][flags(1)]
 *     [batt_mv_hi(1)][batt_mv_lo(1)][batt_pct(1)][tel_flags(1)]
 *     [signature(64)]
 *     Total: 134 bytes. Signature covers the first 70 bytes so the
 *     telemetry readings are authenticated by the sender's Ed25519 key.
 *
 * Parsers negotiate shape from the flags byte; legacy 130-byte announces
 * stay bit-for-bit identical so the wire format is backward compatible
 * with any node that never sets the telemetry bit.
 */
#ifndef NEXUS_ANNOUNCE_H
#define NEXUS_ANNOUNCE_H

#include "types.h"

#define NX_ANNOUNCE_SIGNED_LEN  (NX_PUBKEY_SIZE + NX_PUBKEY_SIZE + 1 + 1) /* 66 */
#define NX_ANNOUNCE_PAYLOAD_LEN (NX_ANNOUNCE_SIGNED_LEN + NX_SIGNATURE_SIZE) /* 130 */

/* Telemetry trailer (inside the signed region) */
#define NX_ANNOUNCE_TELEMETRY_LEN         4   /* batt_mv(2) + pct(1) + flags(1) */
#define NX_ANNOUNCE_SIGNED_LEN_TELEMETRY  (NX_ANNOUNCE_SIGNED_LEN + NX_ANNOUNCE_TELEMETRY_LEN)   /* 70 */
#define NX_ANNOUNCE_PAYLOAD_LEN_TELEMETRY (NX_ANNOUNCE_SIGNED_LEN_TELEMETRY + NX_SIGNATURE_SIZE) /* 134 */

/* Announce flags (extensible) */
#define NX_ANNOUNCE_FLAG_NONE      0x00
#define NX_ANNOUNCE_FLAG_LEAVING   0x01  /* Node is shutting down */
#define NX_ANNOUNCE_FLAG_TELEMETRY 0x02  /* 4-byte telemetry trailer present */

/* Telemetry subfield flags (reserved for future use, e.g. low-power bit) */
#define NX_TELEMETRY_FLAG_NONE     0x00

typedef struct {
    uint16_t battery_mv;   /* 0 if unknown. big-endian on wire. */
    uint8_t  battery_pct;  /* 0..100, 0xFF = unknown */
    uint8_t  flags;
} nx_announce_telemetry_t;

typedef struct {
    uint8_t       sign_pubkey[NX_PUBKEY_SIZE];
    uint8_t       x25519_pubkey[NX_PUBKEY_SIZE];
    nx_role_t     role;
    uint8_t       flags;
    uint8_t       signature[NX_SIGNATURE_SIZE];
    /* Telemetry — populated iff flags & NX_ANNOUNCE_FLAG_TELEMETRY */
    bool                    has_telemetry;
    nx_announce_telemetry_t telemetry;
    /* Derived on receive */
    nx_addr_full_t  full_addr;
    nx_addr_short_t short_addr;
} nx_announce_t;

/*
 * Create a signed announcement from our identity.
 * Writes NX_ANNOUNCE_PAYLOAD_LEN bytes into out_payload.
 * Legacy wrapper — omits telemetry and clears the telemetry flag bit.
 */
nx_err_t nx_announce_create(const nx_identity_t *id, nx_role_t role,
                            uint8_t flags,
                            uint8_t *out_payload, size_t buf_len);

/*
 * Extended variant that can embed a telemetry trailer. Pass telem=NULL
 * to produce a legacy 130-byte announce (telemetry flag bit is forced
 * off in that case). On success *out_len is the number of payload bytes
 * written (130 or 134).
 */
nx_err_t nx_announce_create_ex(const nx_identity_t *id, nx_role_t role,
                               uint8_t flags,
                               const nx_announce_telemetry_t *telem,
                               uint8_t *out_payload, size_t buf_len,
                               size_t *out_len);

/*
 * Parse and verify an announcement payload.
 * Accepts both 130-byte and 134-byte shapes; shape is selected by the
 * NX_ANNOUNCE_FLAG_TELEMETRY bit. Returns NX_ERR_AUTH_FAIL if signature
 * is invalid. On success, ann->full_addr, ann->short_addr and (when
 * present) ann->telemetry are filled.
 */
nx_err_t nx_announce_parse(const uint8_t *payload, size_t len,
                           nx_announce_t *ann);

/*
 * Build a complete announce packet ready for transmission.
 * Sets pkt header fields (PTYPE_ANNOUNCE, RTYPE_FLOOD, src, broadcast dst).
 * Legacy wrapper — no telemetry.
 */
nx_err_t nx_announce_build_packet(const nx_identity_t *id, nx_role_t role,
                                  uint8_t ttl, nx_packet_t *pkt);

/*
 * Extended variant that embeds telemetry when non-NULL.
 */
nx_err_t nx_announce_build_packet_ex(const nx_identity_t *id, nx_role_t role,
                                     uint8_t ttl,
                                     const nx_announce_telemetry_t *telem,
                                     nx_packet_t *pkt);

#endif /* NEXUS_ANNOUNCE_H */
