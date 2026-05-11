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

/* Settings structure stored in flash.
 *
 * The `version` field is checked alongside the magic on load. If the
 * value on flash doesn't match NX_SETTINGS_VERSION the settings are
 * discarded and defaults are used -- this prevents loading a struct
 * laid out by a previous firmware as if it were the current one (a
 * past silent-misload that masked bugs because the magic still matched
 * but field offsets had shifted). Bump NX_SETTINGS_VERSION any time
 * fields are added, removed, or reordered.
 *
 * led_off was carved out of the previous _reserved area without a
 * version bump because both layouts had the same size and the new byte
 * defaulted to 0 (LEDs on) safely. Future layout changes should bump
 * the version. */
typedef struct {
    uint32_t         magic;             /* NX_SETTINGS_MAGIC */
    uint32_t         version;           /* NX_SETTINGS_VERSION */
    uint32_t         screen_timeout_ms; /* 0 = never off */
    nx_lora_config_t lora_config;
    uint8_t          node_role;
    uint8_t          led_off;           /* 1 = disable runtime LED blinks */
    uint8_t          _reserved[2];
} nx_settings_t;

#define NX_SETTINGS_MAGIC   0x4E585354  /* "NXST" */
#define NX_SETTINGS_VERSION 2u          /* Bump on layout change */

/* Default settings */
#define NX_SETTINGS_DEFAULT { \
    .magic             = NX_SETTINGS_MAGIC, \
    .version           = NX_SETTINGS_VERSION, \
    .screen_timeout_ms = 60000, \
    .lora_config       = NX_LORA_CONFIG_DEFAULT, \
    .node_role         = 1, /* NX_ROLE_RELAY */ \
    .led_off           = 0, \
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
