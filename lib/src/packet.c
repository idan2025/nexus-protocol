/*
 * NEXUS Protocol -- Packet Format (13-byte compact header)
 *
 * Wire format:
 * Offset  Size  Field
 * 0       1     flags
 * 1       1     hop_ttl
 * 2       4     dst_short (big-endian)
 * 6       4     src_short (big-endian)
 * 10      2     seq_id (big-endian)
 * 12      1     payload_len
 * 13+     N     payload
 */
#include "nexus/packet.h"

#include <string.h>

uint8_t nx_packet_flags(bool frag, bool exthdr,
                        nx_prio_t prio, nx_ptype_t ptype, nx_rtype_t rtype)
{
    uint8_t f = 0;
    if (frag)   f |= NX_FLAG_FRAG;
    if (exthdr) f |= NX_FLAG_EXTHDR;
    f |= ((uint8_t)prio  << NX_FLAG_PRIO_SHIFT) & NX_FLAG_PRIO_MASK;
    f |= ((uint8_t)ptype << NX_FLAG_PTYPE_SHIFT) & NX_FLAG_PTYPE_MASK;
    f |= ((uint8_t)rtype << NX_FLAG_RTYPE_SHIFT) & NX_FLAG_RTYPE_MASK;
    return f;
}

uint8_t nx_packet_hop_ttl(uint8_t hop_count, uint8_t ttl)
{
    return (uint8_t)((hop_count & 0x0F) << 4) | (ttl & 0x0F);
}

/* Big-endian helpers */
static void write_be16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static uint16_t read_be16(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] << 8 | (uint16_t)buf[1]);
}

int nx_packet_serialize(const nx_packet_t *pkt, uint8_t *buf, size_t buf_len)
{
    if (!pkt || !buf) return NX_ERR_INVALID_ARG;

    size_t total = NX_HEADER_SIZE + pkt->header.payload_len;
    if (buf_len < total) return NX_ERR_BUFFER_TOO_SMALL;

    buf[0] = pkt->header.flags;
    buf[1] = nx_packet_hop_ttl(pkt->header.hop_count, pkt->header.ttl);
    memcpy(&buf[2], pkt->header.dst.bytes, NX_SHORT_ADDR_SIZE);
    memcpy(&buf[6], pkt->header.src.bytes, NX_SHORT_ADDR_SIZE);
    write_be16(&buf[10], pkt->header.seq_id);
    buf[12] = pkt->header.payload_len;

    if (pkt->header.payload_len > 0) {
        memcpy(&buf[NX_HEADER_SIZE], pkt->payload, pkt->header.payload_len);
    }

    return (int)total;
}

nx_err_t nx_packet_deserialize(const uint8_t *buf, size_t buf_len,
                               nx_packet_t *pkt)
{
    if (!buf || !pkt) return NX_ERR_INVALID_ARG;
    if (buf_len < NX_HEADER_SIZE) return NX_ERR_BUFFER_TOO_SMALL;

    memset(pkt, 0, sizeof(*pkt));

    pkt->header.flags     = buf[0];
    pkt->header.hop_count = nx_packet_hop_count(buf[1]);
    pkt->header.ttl       = nx_packet_ttl(buf[1]);
    memcpy(pkt->header.dst.bytes, &buf[2], NX_SHORT_ADDR_SIZE);
    memcpy(pkt->header.src.bytes, &buf[6], NX_SHORT_ADDR_SIZE);
    pkt->header.seq_id      = read_be16(&buf[10]);
    pkt->header.payload_len = buf[12];

    size_t needed = NX_HEADER_SIZE + pkt->header.payload_len;
    if (buf_len < needed) return NX_ERR_BUFFER_TOO_SMALL;

    if (pkt->header.payload_len > 0) {
        memcpy(pkt->payload, &buf[NX_HEADER_SIZE], pkt->header.payload_len);
    }

    return NX_OK;
}

size_t nx_packet_wire_size(const nx_packet_t *pkt)
{
    if (!pkt) return 0;
    return NX_HEADER_SIZE + pkt->header.payload_len;
}
