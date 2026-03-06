/*
 * NEXUS Protocol -- Adaptive Spreading Factor (ASF) Header
 */

#ifndef NEXUS_LORA_ASF_H
#define NEXUS_LORA_ASF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Strategy modes */
typedef enum {
    NX_ASF_STRATEGY_CONSERVATIVE = 0,
    NX_ASF_STRATEGY_BALANCED = 1,
    NX_ASF_STRATEGY_AGGRESSIVE = 2,
    NX_ASF_STRATEGY_ADAPTIVE = 3,
} nx_asf_strategy_t;

/* Opaque ASF state */
typedef struct nx_asf_state nx_asf_state_t;

/* Initialize ASF with given strategy */
nx_asf_state_t* nx_asf_create(nx_asf_strategy_t strategy, uint8_t initial_sf);
void nx_asf_destroy(nx_asf_state_t *asf);

/* Record link events */
void nx_asf_record_tx(nx_asf_state_t *asf, uint8_t sf_used);
void nx_asf_record_rx(nx_asf_state_t *asf, int8_t rssi, int8_t snr, uint8_t sf_used);
void nx_asf_record_ack(nx_asf_state_t *asf, bool success);

/* Get recommended SF (may trigger adaptation) */
uint8_t nx_asf_get_recommended_sf(nx_asf_state_t *asf);

/* Configuration */
void nx_asf_set_bounds(nx_asf_state_t *asf, uint8_t min_sf, uint8_t max_sf);
void nx_asf_set_strategy(nx_asf_state_t *asf, nx_asf_strategy_t strategy);
void nx_asf_set_target_airtime(nx_asf_state_t *asf, uint32_t percent);

/* Status */
nx_asf_strategy_t nx_asf_get_strategy(const nx_asf_state_t *asf);
uint8_t nx_asf_get_current_sf(const nx_asf_state_t *asf);

/* Statistics */
void nx_asf_get_stats(nx_asf_state_t *asf, uint32_t *sent, uint32_t *acked,
                      uint32_t *lost, uint8_t *success_rate);
void nx_asf_get_link_quality(nx_asf_state_t *asf, int8_t *rssi, int8_t *snr);

/* Utilities */
uint32_t nx_asf_estimate_airtime(uint8_t sf, size_t payload_len);
void nx_asf_force_sf(nx_asf_state_t *asf, uint8_t sf);
void nx_asf_reset(nx_asf_state_t *asf);
const char* nx_asf_strategy_name(nx_asf_strategy_t strategy);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_LORA_ASF_H */
