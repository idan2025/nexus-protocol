/*
 * NEXUS Protocol -- Identity Announcements
 *
 * Announcements let nodes discover each other. Each announcement is
 * Ed25519-signed by the sender so receivers can verify authenticity and
 * derive addresses from the included public key.
 *
 * Wire payload (inside a PTYPE_ANNOUNCE packet):
 *   [sign_pubkey(32)][x25519_pubkey(32)][role(1)][flags(1)][signature(64)]
 *   Total: 130 bytes (fits in NX_MAX_PAYLOAD = 242)
 *
 * The signature covers: sign_pubkey || x25519_pubkey || role || flags
 */
#ifndef NEXUS_ANNOUNCE_H
#define NEXUS_ANNOUNCE_H

#include "types.h"

#define NX_ANNOUNCE_SIGNED_LEN  (NX_PUBKEY_SIZE + NX_PUBKEY_SIZE + 1 + 1) /* 66 */
#define NX_ANNOUNCE_PAYLOAD_LEN (NX_ANNOUNCE_SIGNED_LEN + NX_SIGNATURE_SIZE) /* 130 */

/* Announce flags (extensible) */
#define NX_ANNOUNCE_FLAG_NONE      0x00
#define NX_ANNOUNCE_FLAG_LEAVING   0x01  /* Node is shutting down */

typedef struct {
    uint8_t       sign_pubkey[NX_PUBKEY_SIZE];
    uint8_t       x25519_pubkey[NX_PUBKEY_SIZE];
    nx_role_t     role;
    uint8_t       flags;
    uint8_t       signature[NX_SIGNATURE_SIZE];
    /* Derived on receive */
    nx_addr_full_t  full_addr;
    nx_addr_short_t short_addr;
} nx_announce_t;

/*
 * Create a signed announcement from our identity.
 * Writes NX_ANNOUNCE_PAYLOAD_LEN bytes into out_payload.
 */
nx_err_t nx_announce_create(const nx_identity_t *id, nx_role_t role,
                            uint8_t flags,
                            uint8_t *out_payload, size_t buf_len);

/*
 * Parse and verify an announcement payload.
 * Returns NX_ERR_AUTH_FAIL if signature is invalid.
 * On success, ann->full_addr and ann->short_addr are derived.
 */
nx_err_t nx_announce_parse(const uint8_t *payload, size_t len,
                           nx_announce_t *ann);

/*
 * Build a complete announce packet ready for transmission.
 * Sets pkt header fields (PTYPE_ANNOUNCE, RTYPE_FLOOD, src, broadcast dst).
 */
nx_err_t nx_announce_build_packet(const nx_identity_t *id, nx_role_t role,
                                  uint8_t ttl, nx_packet_t *pkt);

#endif /* NEXUS_ANNOUNCE_H */
