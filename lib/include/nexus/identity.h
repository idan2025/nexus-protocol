/*
 * NEXUS Protocol -- Identity (Key Generation & Address Derivation)
 */
#ifndef NEXUS_IDENTITY_H
#define NEXUS_IDENTITY_H

#include "types.h"

/* Generate a new random identity (Ed25519 + X25519 keypairs + addresses). */
nx_err_t nx_identity_generate(nx_identity_t *id);

/* Derive full address (16-byte BLAKE2b of Ed25519 public key). */
void nx_identity_derive_full_addr(const uint8_t pubkey[NX_PUBKEY_SIZE],
                                  nx_addr_full_t *out);

/* Derive short address (first 4 bytes of full address). */
void nx_identity_derive_short_addr(const nx_addr_full_t *full,
                                   nx_addr_short_t *out);

/* Wipe all secret key material from an identity. */
void nx_identity_wipe(nx_identity_t *id);

/* Compare two short addresses. Returns 0 if equal. */
int nx_addr_short_cmp(const nx_addr_short_t *a, const nx_addr_short_t *b);

/* Compare two full addresses. Returns 0 if equal. */
int nx_addr_full_cmp(const nx_addr_full_t *a, const nx_addr_full_t *b);

#endif /* NEXUS_IDENTITY_H */
