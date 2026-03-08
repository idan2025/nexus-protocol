/*
 * NEXUS Firmware -- Settings Persistence
 *
 * Save/load node settings (radio config, screen timeout, role) to flash.
 * ESP32: NVS (Preferences API)
 * nRF52: InternalFS (LittleFS)
 */
#ifndef NEXUS_SETTINGS_STORE_H
#define NEXUS_SETTINGS_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/types.h"
#include "nexus/lora_radio.h"

/* Settings structure stored in flash */
typedef struct {
    uint32_t         magic;             /* NX_SETTINGS_MAGIC */
    uint32_t         screen_timeout_ms; /* 0 = never off */
    nx_lora_config_t lora_config;
    uint8_t          node_role;
    uint8_t          _reserved[3];
} nx_settings_t;

#define NX_SETTINGS_MAGIC 0x4E585354  /* "NXST" */

/* Default settings */
#define NX_SETTINGS_DEFAULT { \
    .magic             = NX_SETTINGS_MAGIC, \
    .screen_timeout_ms = 60000, \
    .lora_config       = NX_LORA_CONFIG_DEFAULT, \
    .node_role         = 1, /* NX_ROLE_RELAY */ \
    ._reserved         = {0}, \
}

/* Load settings from flash. Returns NX_ERR_NOT_FOUND on first boot. */
nx_err_t nx_settings_load(nx_settings_t *settings);

/* Save settings to flash. */
nx_err_t nx_settings_save(const nx_settings_t *settings);

/* Erase stored settings (revert to defaults on next boot). */
nx_err_t nx_settings_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_SETTINGS_STORE_H */
