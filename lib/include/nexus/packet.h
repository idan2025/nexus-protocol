/*
 * NEXUS Protocol -- Packet Format (13-byte compact header)
 */
#ifndef NEXUS_PACKET_H
#define NEXUS_PACKET_H

#include "types.h"

/* ── Header Construction ─────────────────────────────────────────────── */

/* Build the flags byte from component fields. */
uint8_t nx_packet_flags(bool frag, bool exthdr,
                        nx_prio_t prio, nx_ptype_t ptype, nx_rtype_t rtype);

/* Encode hop_count (upper nibble) and ttl (lower nibble) into one byte. */
uint8_t nx_packet_hop_ttl(uint8_t hop_count, uint8_t ttl);

/* ── Serialization ───────────────────────────────────────────────────── */

/*
 * Serialize a packet (header + payload) into a wire-format buffer.
 * Returns total bytes written, or negative nx_err_t on failure.
 * buf must be at least NX_HEADER_SIZE + pkt->header.payload_len bytes.
 */
int nx_packet_serialize(const nx_packet_t *pkt, uint8_t *buf, size_t buf_len);

/*
 * Deserialize wire-format bytes into a packet struct.
 * Returns NX_OK or negative nx_err_t on failure.
 */
nx_err_t nx_packet_deserialize(const uint8_t *buf, size_t buf_len,
                               nx_packet_t *pkt);

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Total wire size of a packet (header + payload). */
size_t nx_packet_wire_size(const nx_packet_t *pkt);

/* Decode flag fields. */
static inline bool       nx_packet_flag_frag(uint8_t flags)   { return (flags & NX_FLAG_FRAG) != 0; }
static inline bool       nx_packet_flag_exthdr(uint8_t flags) { return (flags & NX_FLAG_EXTHDR) != 0; }
static inline nx_prio_t  nx_packet_flag_prio(uint8_t flags)   { return (nx_prio_t)((flags & NX_FLAG_PRIO_MASK) >> NX_FLAG_PRIO_SHIFT); }
static inline nx_ptype_t nx_packet_flag_ptype(uint8_t flags)  { return (nx_ptype_t)((flags & NX_FLAG_PTYPE_MASK) >> NX_FLAG_PTYPE_SHIFT); }
static inline nx_rtype_t nx_packet_flag_rtype(uint8_t flags)  { return (nx_rtype_t)((flags & NX_FLAG_RTYPE_MASK) >> NX_FLAG_RTYPE_SHIFT); }

/* Decode hop_ttl byte. */
static inline uint8_t nx_packet_hop_count(uint8_t hop_ttl) { return (hop_ttl >> 4) & 0x0F; }
static inline uint8_t nx_packet_ttl(uint8_t hop_ttl)       { return hop_ttl & 0x0F; }

#endif /* NEXUS_PACKET_H */
