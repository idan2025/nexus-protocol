/*
 * NEXUS Firmware -- Identity Persistence
 *
 * Save/load identity keys to non-volatile storage.
 * ESP32: NVS (Non-Volatile Storage)
 * nRF52: Internal flash
 */
#ifndef NEXUS_IDENTITY_STORE_H
#define NEXUS_IDENTITY_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/identity.h"

/* Load identity from flash. Returns NX_OK if found, NX_ERR_NOT_FOUND if
 * no identity saved yet (first boot). */
nx_err_t nx_identity_store_load(nx_identity_t *id);

/* Save identity to flash. Overwrites any existing identity. */
nx_err_t nx_identity_store_save(const nx_identity_t *id);

/* Erase stored identity from flash. */
nx_err_t nx_identity_store_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_IDENTITY_STORE_H */
