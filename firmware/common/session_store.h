/*
 * NEXUS Firmware -- Session Store Persistence
 *
 * Save/load Double Ratchet sessions so conversations survive reboot.
 * ESP32: NVS (Preferences), one entry per valid slot ("s0".."s15").
 * nRF52: InternalFS (LittleFS), one file per valid slot ("/nxs_0".."/nxs_15").
 *
 * SECURITY: sessions contain ratchet keys — flash contents are as
 * sensitive as the identity key. Physical flash access => decryption
 * of past ciphertext. Encrypt-at-rest is the caller's responsibility.
 */
#ifndef NEXUS_SESSION_STORE_H
#define NEXUS_SESSION_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/session.h"

/* Save every valid session slot. Call after a handshake completes or
 * sparingly on a timer — every encrypt would burn flash. */
nx_err_t nx_session_store_save(const nx_session_store_t *store);

/* Load every session from flash into `store`. Store must be initialised
 * (nx_session_store_init) before calling. Returns NX_ERR_NOT_FOUND if
 * nothing was loaded. */
nx_err_t nx_session_store_load(nx_session_store_t *store);

/* Wipe all persisted sessions. */
nx_err_t nx_session_store_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_SESSION_STORE_H */
