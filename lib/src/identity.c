/*
 * NEXUS Protocol -- Identity (Key Generation & Address Derivation)
 */
#include "nexus/identity.h"
#include "nexus/platform.h"
#include "monocypher/monocypher.h"

#include <string.h>

nx_err_t nx_identity_generate(nx_identity_t *id)
{
    if (!id) return NX_ERR_INVALID_ARG;

    memset(id, 0, sizeof(*id));

    /* Generate Ed25519 keypair */
    uint8_t seed[32];
    nx_err_t err = nx_platform_random(seed, sizeof(seed));
    if (err != NX_OK) return err;

    crypto_eddsa_key_pair(id->sign_secret, id->sign_public, seed);
    crypto_wipe(seed, sizeof(seed));

    /* Derive X25519 keypair from Ed25519 keys */
    crypto_eddsa_to_x25519(id->x25519_secret, id->sign_secret);
    crypto_x25519_public_key(id->x25519_public, id->x25519_secret);

    /* Derive addresses */
    nx_identity_derive_full_addr(id->sign_public, &id->full_addr);
    nx_identity_derive_short_addr(&id->full_addr, &id->short_addr);

    return NX_OK;
}

void nx_identity_derive_full_addr(const uint8_t pubkey[NX_PUBKEY_SIZE],
                                  nx_addr_full_t *out)
{
    /* BLAKE2b-128 of the Ed25519 public key */
    crypto_blake2b(out->bytes, NX_FULL_ADDR_SIZE,
                   pubkey, NX_PUBKEY_SIZE);
}

void nx_identity_derive_short_addr(const nx_addr_full_t *full,
                                   nx_addr_short_t *out)
{
    memcpy(out->bytes, full->bytes, NX_SHORT_ADDR_SIZE);
}

void nx_identity_wipe(nx_identity_t *id)
{
    if (!id) return;
    crypto_wipe(id, sizeof(*id));
}

int nx_addr_short_cmp(const nx_addr_short_t *a, const nx_addr_short_t *b)
{
    return memcmp(a->bytes, b->bytes, NX_SHORT_ADDR_SIZE);
}

int nx_addr_full_cmp(const nx_addr_full_t *a, const nx_addr_full_t *b)
{
    return memcmp(a->bytes, b->bytes, NX_FULL_ADDR_SIZE);
}
