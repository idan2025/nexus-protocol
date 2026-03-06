/*
 * NEXUS Protocol -- Identity Announcements
 */
#include "nexus/announce.h"
#include "nexus/identity.h"
#include "nexus/packet.h"
#include "monocypher/monocypher.h"

#include <string.h>

nx_err_t nx_announce_create(const nx_identity_t *id, nx_role_t role,
                            uint8_t flags,
                            uint8_t *out_payload, size_t buf_len)
{
    if (!id || !out_payload) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_ANNOUNCE_PAYLOAD_LEN) return NX_ERR_BUFFER_TOO_SMALL;

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

    /* Sign: sign_pubkey || x25519_pubkey || role || flags */
    crypto_eddsa_sign(p, id->sign_secret, out_payload, NX_ANNOUNCE_SIGNED_LEN);

    return NX_OK;
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

    memcpy(ann->signature, p, NX_SIGNATURE_SIZE);

    /* Verify signature over the signed portion */
    int ret = crypto_eddsa_check(ann->signature, ann->sign_pubkey,
                                 payload, NX_ANNOUNCE_SIGNED_LEN);
    if (ret != 0) return NX_ERR_AUTH_FAIL;

    /* Derive addresses */
    nx_identity_derive_full_addr(ann->sign_pubkey, &ann->full_addr);
    nx_identity_derive_short_addr(&ann->full_addr, &ann->short_addr);

    return NX_OK;
}

nx_err_t nx_announce_build_packet(const nx_identity_t *id, nx_role_t role,
                                  uint8_t ttl, nx_packet_t *pkt)
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
    pkt->header.payload_len = NX_ANNOUNCE_PAYLOAD_LEN;

    return nx_announce_create(id, role, NX_ANNOUNCE_FLAG_NONE,
                              pkt->payload, sizeof(pkt->payload));
}
