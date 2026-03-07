/*
 * NEXUS Firmware -- Anchor Mailbox Persistence
 *
 * Save/load stored messages to non-volatile storage so they survive reboot.
 * ESP32: NVS (Preferences API)
 * nRF52: Internal flash (LittleFS)
 */
#ifndef NEXUS_ANCHOR_STORE_H
#define NEXUS_ANCHOR_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/anchor.h"

/* Save all valid anchor messages to flash.
 * Call periodically (e.g., every 10-30s) when anchor count changes. */
nx_err_t nx_anchor_store_save(const nx_anchor_t *anchor);

/* Load anchor messages from flash into the mailbox.
 * Call once at boot, after nx_anchor_init() and nx_anchor_configure_for_role(). */
nx_err_t nx_anchor_store_load(nx_anchor_t *anchor);

/* Erase all stored anchor messages from flash. */
nx_err_t nx_anchor_store_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_ANCHOR_STORE_H */
