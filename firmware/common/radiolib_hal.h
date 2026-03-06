/*
 * NEXUS Firmware -- RadioLib HAL Bridge
 *
 * Implements nx_lora_radio_ops_t using RadioLib's SX1262 driver.
 * Works with any board that has an SX126x radio (Heltec V3, RAK4631, etc.)
 */
#ifndef NEXUS_RADIOLIB_HAL_H
#define NEXUS_RADIOLIB_HAL_H

#include <RadioLib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/lora_radio.h"
#include "nexus/transport.h"

#ifdef __cplusplus
}
#endif

/* Create a NEXUS LoRa radio backed by a RadioLib SX1262 instance.
 * The SX1262 must already be constructed (Module pins set up).
 * Caller owns both the SX1262 and the returned radio. */
nx_lora_radio_t *nx_radiolib_create(SX1262 *radio);

/* Create and register a NEXUS LoRa transport using this radio.
 * Convenience function: creates radio HAL + transport, inits both,
 * registers with transport registry. Returns the transport pointer. */
nx_transport_t *nx_radiolib_transport_setup(SX1262 *radio,
                                             const nx_lora_config_t *config);

#endif /* NEXUS_RADIOLIB_HAL_H */
