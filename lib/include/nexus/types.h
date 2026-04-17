/*
 * NEXUS Protocol -- Core Types, Constants, and Error Codes
 * Network EXchange Unified System
 */
#ifndef NEXUS_TYPES_H
#define NEXUS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Version ─────────────────────────────────────────────────────────── */
#define NX_VERSION_MAJOR  0
#define NX_VERSION_MINOR  1
#define NX_VERSION_PATCH  0

/* ── Sizes ───────────────────────────────────────────────────────────── */
#define NX_SHORT_ADDR_SIZE    4
#define NX_FULL_ADDR_SIZE    16
#define NX_PUBKEY_SIZE       32
#define NX_PRIVKEY_SIZE      32
#define NX_SIGN_SECRET_SIZE  64   /* Ed25519 expanded secret key */
#define NX_SIGNATURE_SIZE    64
#define NX_SHARED_SECRET_SIZE 32
#define NX_SYMMETRIC_KEY_SIZE 32
#define NX_NONCE_SIZE        24   /* XChaCha20 nonce */
#define NX_MAC_SIZE          16   /* Poly1305 tag */
#define NX_HEADER_SIZE       13
#define NX_MAX_PAYLOAD       242
#define NX_MAX_PACKET        (NX_HEADER_SIZE + NX_MAX_PAYLOAD + NX_MAC_SIZE)
#define NX_SEQ_ID_SIZE        2

/* ── Error Codes ─────────────────────────────────────────────────────── */
typedef enum {
    NX_OK = 0,
    NX_ERR_INVALID_ARG    = -1,
    NX_ERR_BUFFER_TOO_SMALL = -2,
    NX_ERR_CRYPTO_FAIL    = -3,
    NX_ERR_AUTH_FAIL      = -4,   /* MAC verification failed */
    NX_ERR_NO_MEMORY      = -5,
    NX_ERR_NOT_FOUND      = -6,
    NX_ERR_TRANSPORT      = -7,
    NX_ERR_TIMEOUT        = -8,
    NX_ERR_FULL           = -9,
    NX_ERR_ALREADY_EXISTS = -10,
    NX_ERR_IO             = -11,
} nx_err_t;

/* ── Node Roles ──────────────────────────────────────────────────────── */
typedef enum {
    NX_ROLE_LEAF     = 0,  /* Never relays */
    NX_ROLE_RELAY    = 1,  /* Forwarding + basic store-and-forward */
    NX_ROLE_GATEWAY  = 2,  /* Cross-transport bridge (legacy, same as RELAY) */
    NX_ROLE_ANCHOR   = 3,  /* Store-and-forward (small buffer) */
    NX_ROLE_SENTINEL = 4,  /* All capabilities */
    NX_ROLE_PILLAR   = 5,  /* Public internet relay (accepts TCP connections) */
    NX_ROLE_VAULT    = 6,  /* Enhanced store-and-forward (large buffer, long TTL) */
} nx_role_t;

/* ── Packet Type (2 bits in flags) ───────────────────────────────────── */
typedef enum {
    NX_PTYPE_DATA      = 0,  /* Encrypted user data */
    NX_PTYPE_ANNOUNCE  = 1,  /* Identity announcement */
    NX_PTYPE_ROUTE     = 2,  /* Routing control (PRISM) */
    NX_PTYPE_ACK       = 3,  /* Acknowledgement */
} nx_ptype_t;

/* ── Routing Type (2 bits in flags) ──────────────────────────────────── */
typedef enum {
    NX_RTYPE_DIRECT    = 0,  /* Direct / single-hop */
    NX_RTYPE_FLOOD     = 1,  /* Flood (limited by TTL) */
    NX_RTYPE_ROUTED    = 2,  /* Source-routed via PRISM */
    NX_RTYPE_DOMAIN    = 3,  /* Cross-domain via gateway */
} nx_rtype_t;

/* ── Priority (2 bits in flags) ──────────────────────────────────────── */
typedef enum {
    NX_PRIO_LOW     = 0,
    NX_PRIO_NORMAL  = 1,
    NX_PRIO_HIGH    = 2,
    NX_PRIO_URGENT  = 3,
} nx_prio_t;

/* ── Flag Bits ───────────────────────────────────────────────────────── */
#define NX_FLAG_FRAG     (1 << 7)  /* Fragment follows */
#define NX_FLAG_EXTHDR   (1 << 6)  /* Extended header present */
#define NX_FLAG_PRIO_SHIFT   4
#define NX_FLAG_PRIO_MASK    0x30
#define NX_FLAG_PTYPE_SHIFT  2
#define NX_FLAG_PTYPE_MASK   0x0C
#define NX_FLAG_RTYPE_SHIFT  0
#define NX_FLAG_RTYPE_MASK   0x03

/* ── Extended Header Types ──────────────────────────────────────────── */
#define NX_EXTHDR_FRAGMENT   0x01
/* 0x10-0x12 defined in session.h (SESSION_SUB_*) */
/* 0x20 defined in group.h (GROUP_MSG) */
#define NX_EXTHDR_INBOX_REQ  0x30  /* Client asking a pillar/anchor to replay stored pkts for src */
#define NX_EXTHDR_FED_DIGEST 0x31  /* Pillar gossiping msg-ids it currently stores */
#define NX_EXTHDR_FED_FETCH  0x32  /* Pillar requesting specific msg-ids listed in a prior DIGEST */

/* ── Addresses ───────────────────────────────────────────────────────── */
typedef struct {
    uint8_t bytes[NX_FULL_ADDR_SIZE];
} nx_addr_full_t;

typedef struct {
    uint8_t bytes[NX_SHORT_ADDR_SIZE];
} nx_addr_short_t;

/* ── Identity ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t       sign_secret[NX_SIGN_SECRET_SIZE]; /* Ed25519 secret (64B) */
    uint8_t       sign_public[NX_PUBKEY_SIZE];      /* Ed25519 public (32B) */
    uint8_t       x25519_secret[NX_PRIVKEY_SIZE];   /* X25519 secret (32B) */
    uint8_t       x25519_public[NX_PUBKEY_SIZE];    /* X25519 public (32B) */
    nx_addr_full_t  full_addr;                       /* BLAKE2b(sign_public) */
    nx_addr_short_t short_addr;                      /* First 4 bytes of full */
} nx_identity_t;

/* ── Packet Header ───────────────────────────────────────────────────── */
typedef struct {
    uint8_t        flags;
    uint8_t        hop_count;     /* Upper nibble of hop_ttl */
    uint8_t        ttl;           /* Lower nibble of hop_ttl */
    nx_addr_short_t dst;
    nx_addr_short_t src;
    uint16_t       seq_id;
    uint8_t        payload_len;
} nx_header_t;

typedef struct {
    nx_header_t header;
    uint8_t     payload[NX_MAX_PAYLOAD + NX_MAC_SIZE];
} nx_packet_t;

/* ── Broadcast Address ───────────────────────────────────────────────── */
#define NX_ADDR_BROADCAST_SHORT  ((nx_addr_short_t){{0xFF, 0xFF, 0xFF, 0xFF}})

#endif /* NEXUS_TYPES_H */
