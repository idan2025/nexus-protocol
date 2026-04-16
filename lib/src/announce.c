/*
 * NEXUS Protocol -- Identity Announcements
 */
#include "nexus/announce.h"
#include "nexus/identity.h"
#include "nexus/packet.h"
#include "monocypher/monocypher.h"

#include <string.h>

nx_err_t nx_announce_create_ex(const nx_identity_t *id, nx_role_t role,
                               uint8_t flags,
                               const nx_announce_telemetry_t *telem,
                               uint8_t *out_payload, size_t buf_len,
                               size_t *out_len)
{
    if (!id || !out_payload) return NX_ERR_INVALID_ARG;

    /* Telemetry flag must match the telem pointer. We do the bookkeeping
     * so callers can just pass telem=NULL to skip it without having to
     * remember to clear the flag bit. */
    if (telem) {
        flags |= NX_ANNOUNCE_FLAG_TELEMETRY;
    } else {
        flags &= ~NX_ANNOUNCE_FLAG_TELEMETRY;
    }

    size_t signed_len  = telem ? NX_ANNOUNCE_SIGNED_LEN_TELEMETRY
                               : NX_ANNOUNCE_SIGNED_LEN;
    size_t payload_len = signed_len + NX_SIGNATURE_SIZE;
    if (buf_len < payload_len) return NX_ERR_BUFFER_TOO_SMALL;

    uint8_t *p = out_payload;

    /* sign_pubkey(32) */
    memcpy(p, id->sign_public, NX_PUBKEY_SIZE);
    p += NX_PUBKEY_SIZE;

    /* x25519_pubkey(32) */
    memcpy(p, id->x25519_public, NX_PUBKEY_SIZE);
    p += NX_PUBKEY_SIZE;

    /* role(1) */
    *p++ = (uint8_t)role;

    /* flags(1) */
    *p++ = flags;

    /* optional telemetry(4) — inside the signed region */
    if (telem) {
        *p++ = (uint8_t)((telem->battery_mv >> 8) & 0xFF);
        *p++ = (uint8_t)(telem->battery_mv & 0xFF);
        *p++ = telem->battery_pct;
        *p++ = telem->flags;
    }

    /* Sign everything up to the signature slot */
    crypto_eddsa_sign(p, id->sign_secret, out_payload, signed_len);

    if (out_len) *out_len = payload_len;
    return NX_OK;
}

nx_err_t nx_announce_create(const nx_identity_t *id, nx_role_t role,
                            uint8_t flags,
                            uint8_t *out_payload, size_t buf_len)
{
    return nx_announce_create_ex(id, role, flags, NULL,
                                 out_payload, buf_len, NULL);
}

nx_err_t nx_announce_parse(const uint8_t *payload, size_t len,
                           nx_announce_t *ann)
{
    if (!payload || !ann) return NX_ERR_INVALID_ARG;
    if (len < NX_ANNOUNCE_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

    memset(ann, 0, sizeof(*ann));

    const uint8_t *p = payload;

    memcpy(ann->sign_pubkey, p, NX_PUBKEY_SIZE);
    p += NX_PUBKEY_SIZE;

    memcpy(ann->x25519_pubkey, p, NX_PUBKEY_SIZE);
    p += NX_PUBKEY_SIZE;

    ann->role = (nx_role_t)*p++;
    ann->flags = *p++;

    bool telem_bit = (ann->flags & NX_ANNOUNCE_FLAG_TELEMETRY) != 0;
    size_t signed_len  = telem_bit ? NX_ANNOUNCE_SIGNED_LEN_TELEMETRY
                                   : NX_ANNOUNCE_SIGNED_LEN;
    size_t payload_len = signed_len + NX_SIGNATURE_SIZE;
    if (len < payload_len) return NX_ERR_BUFFER_TOO_SMALL;

    if (telem_bit) {
        uint8_t hi = *p++;
        uint8_t lo = *p++;
        ann->telemetry.battery_mv  = (uint16_t)((uint16_t)hi << 8) | lo;
        ann->telemetry.battery_pct = *p++;
        ann->telemetry.flags       = *p++;
        ann->has_telemetry = true;
    }

    memcpy(ann->signature, p, NX_SIGNATURE_SIZE);

    /* Verify signature over the signed portion */
    int ret = crypto_eddsa_check(ann->signature, ann->sign_pubkey,
                                 payload, signed_len);
    if (ret != 0) return NX_ERR_AUTH_FAIL;

    /* Derive addresses */
    nx_identity_derive_full_addr(ann->sign_pubkey, &ann->full_addr);
    nx_identity_derive_short_addr(&ann->full_addr, &ann->short_addr);

    return NX_OK;
}

nx_err_t nx_announce_build_packet_ex(const nx_identity_t *id, nx_role_t role,
                                     uint8_t ttl,
                                     const nx_announce_telemetry_t *telem,
                                     nx_packet_t *pkt)
{
    if (!id || !pkt) return NX_ERR_INVALID_ARG;

    memset(pkt, 0, sizeof(*pkt));

    pkt->header.flags = nx_packet_flags(false, false, NX_PRIO_NORMAL,
                                        NX_PTYPE_ANNOUNCE, NX_RTYPE_FLOOD);
    pkt->header.hop_count = 0;
    pkt->header.ttl = ttl;
    pkt->header.dst = NX_ADDR_BROADCAST_SHORT;
    pkt->header.src = id->short_addr;
    pkt->header.seq_id = 0; /* Caller should set */

    size_t written = 0;
    nx_err_t err = nx_announce_create_ex(id, role, NX_ANNOUNCE_FLAG_NONE,
                                         telem,
                                         pkt->payload, sizeof(pkt->payload),
                                         &written);
    if (err != NX_OK) return err;
    pkt->header.payload_len = (uint8_t)written;
    return NX_OK;
}

nx_err_t nx_announce_build_packet(const nx_identity_t *id, nx_role_t role,
                                  uint8_t ttl, nx_packet_t *pkt)
{
    return nx_announce_build_packet_ex(id, role, ttl, NULL, pkt);
}
