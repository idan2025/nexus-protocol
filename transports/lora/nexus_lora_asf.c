/*
 * NEXUS Protocol -- Adaptive Spreading Factor (ASF) for LoRa
 */

#include "nexus/lora_asf.h"
#include "nexus/platform.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* ASF Configuration */
#define NX_ASF_UPDATE_INTERVAL_MS    30000
#define NX_ASF_MIN_SAMPLES            5
#define NX_ASF_HISTORY_SIZE          16

#define NX_ASF_RSSI_STRONG           (-80)
#define NX_ASF_RSSI_WEAK             (-115)
#define NX_ASF_SNR_GOOD              10
#define NX_ASF_SNR_POOR              (-5)
#define NX_ASF_SUCCESS_HIGH           95
#define NX_ASF_SUCCESS_LOW            70
#define NX_ASF_SF_MIN                 7
#define NX_ASF_SF_MAX                 12
#define NX_ASF_SAFETY_MARGIN_DB       3

#ifndef NX_CLAMP
#define NX_CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

/* Packet history entry */
typedef struct {
    uint64_t timestamp_ms;
    int8_t   rssi;
    int8_t   snr;
    bool     success;
    uint8_t  sf_used;
} nx_asf_history_entry_t;

/* ASF state */
struct nx_asf_state {
    nx_asf_strategy_t strategy;
    uint8_t             current_sf;
    uint8_t             min_sf;
    uint8_t             max_sf;
    uint32_t            update_interval_ms;
    nx_asf_history_entry_t history[NX_ASF_HISTORY_SIZE];
    uint8_t              history_index;
    uint8_t              history_count;
    uint64_t            last_update_ms;
    uint32_t            packets_sent;
    uint32_t            packets_acked;
    uint32_t            packets_lost;
    int8_t              avg_rssi;
    int8_t              avg_snr;
    uint8_t             current_success_rate;
    uint32_t            target_airtime_percent;
    uint32_t            current_airtime_ms;
    uint32_t            window_start_ms;
};

/* Airtime per SF at 125kHz BW (ms per byte) */
static const uint32_t nx_asf_airtime_per_byte_ms[13] = {
    0, 0, 0, 0, 0, 0, 0,
    2,   /* SF7 */
    4,   /* SF8 */
    8,   /* SF9 */
    16,  /* SF10 */
    32,  /* SF11 */
    64,  /* SF12 */
};

/* Time on air for different SFs */
static const uint32_t nx_asf_packet_airtime_ms[13] = {
    0, 0, 0, 0, 0, 0, 0,
    41,   /* SF7 */
    72,   /* SF8 */
    144,  /* SF9 */
    297,  /* SF10 */
    582,  /* SF11 */
    1152, /* SF12 */
};

/* Implementation */

nx_asf_state_t* nx_asf_create(nx_asf_strategy_t strategy, uint8_t initial_sf)
{
    nx_asf_state_t *asf = (nx_asf_state_t *)malloc(sizeof(nx_asf_state_t));
    if (!asf) return NULL;
    
    memset(asf, 0, sizeof(*asf));
    
    asf->strategy = strategy;
    asf->current_sf = NX_CLAMP(initial_sf, NX_ASF_SF_MIN, NX_ASF_SF_MAX);
    asf->min_sf = NX_ASF_SF_MIN;
    asf->max_sf = NX_ASF_SF_MAX;
    asf->update_interval_ms = NX_ASF_UPDATE_INTERVAL_MS;
    asf->target_airtime_percent = 80;
    
    asf->last_update_ms = nx_platform_time_ms();
    asf->window_start_ms = nx_platform_time_ms();
    
    return asf;
}

void nx_asf_destroy(nx_asf_state_t *asf)
{
    if (!asf) return;
    memset(asf, 0, sizeof(*asf));
    free(asf);
}

void nx_asf_record_tx(nx_asf_state_t *asf, uint8_t sf_used)
{
    if (!asf) return;
    
    asf->packets_sent++;
    
    uint32_t airtime = nx_asf_packet_airtime_ms[sf_used];
    asf->current_airtime_ms += airtime;
    
    nx_asf_history_entry_t *entry = &asf->history[asf->history_index];
    entry->timestamp_ms = nx_platform_time_ms();
    entry->sf_used = sf_used;
    entry->success = false;
    entry->rssi = 0;
    entry->snr = 0;
    
    asf->history_index = (asf->history_index + 1) % NX_ASF_HISTORY_SIZE;
    if (asf->history_count < NX_ASF_HISTORY_SIZE) {
        asf->history_count++;
    }
}

void nx_asf_record_rx(nx_asf_state_t *asf, int8_t rssi, int8_t snr, uint8_t sf_used)
{
    if (!asf) return;
    
    (void)sf_used;
    
    /* Update running averages */
    int32_t total_rssi = 0;
    int32_t total_snr = 0;
    uint8_t count = 0;
    
    for (uint8_t i = 0; i < NX_ASF_HISTORY_SIZE; i++) {
        if (asf->history[i].timestamp_ms > 0) {
            total_rssi += asf->history[i].rssi;
            total_snr += asf->history[i].snr;
            count++;
        }
    }
    
    /* Add current measurement */
    if (count < NX_ASF_HISTORY_SIZE) {
        asf->history[asf->history_index].rssi = rssi;
        asf->history[asf->history_index].snr = snr;
    }
    
    if (count > 0) {
        asf->avg_rssi = (int8_t)(total_rssi / count);
        asf->avg_snr = (int8_t)(total_snr / count);
    }
}

void nx_asf_record_ack(nx_asf_state_t *asf, bool success)
{
    if (!asf) return;
    
    if (success) {
        asf->packets_acked++;
    } else {
        asf->packets_lost++;
    }
    
    uint32_t success_count = 0;
    uint32_t total = 0;
    
    for (uint8_t i = 0; i < NX_ASF_HISTORY_SIZE; i++) {
        if (asf->history[i].timestamp_ms > 0) {
            total++;
            if (asf->history[i].success) {
                success_count++;
            }
        }
    }
    
    if (total > 0) {
        asf->current_success_rate = (uint8_t)((success_count * 100) / total);
    }
}

static uint8_t nx_asf_calculate_recommended_sf(nx_asf_state_t *asf)
{
    if (!asf) return NX_ASF_SF_MIN;
    
    if (asf->history_count < NX_ASF_MIN_SAMPLES) {
        return asf->current_sf;
    }
    
    uint8_t recommended = asf->current_sf;
    
    uint64_t now = nx_platform_time_ms();
    uint32_t window_duration = now - asf->window_start_ms;
    uint32_t duty_cycle_used = 0;
    
    if (window_duration > 0) {
        duty_cycle_used = (asf->current_airtime_ms * 100) / window_duration;
    }
    
    switch (asf->strategy) {
        case NX_ASF_STRATEGY_CONSERVATIVE:
            if (asf->current_success_rate < NX_ASF_SUCCESS_HIGH ||
                asf->avg_rssi < NX_ASF_RSSI_WEAK + NX_ASF_SAFETY_MARGIN_DB ||
                asf->avg_snr < NX_ASF_SNR_GOOD) {
                if (recommended < NX_ASF_SF_MAX) {
                    recommended++;
                }
            } else if (asf->current_success_rate >= 99 &&
                       asf->avg_rssi > NX_ASF_RSSI_STRONG - 10 &&
                       asf->avg_snr > NX_ASF_SNR_GOOD + 5) {
                if (recommended > NX_ASF_SF_MIN) {
                    recommended--;
                }
            }
            break;
            
        case NX_ASF_STRATEGY_AGGRESSIVE:
            if (asf->current_success_rate >= NX_ASF_SUCCESS_LOW &&
                asf->avg_rssi > NX_ASF_RSSI_WEAK &&
                asf->avg_snr > NX_ASF_SNR_POOR) {
                if (recommended > NX_ASF_SF_MIN) {
                    recommended--;
                }
            } else if (asf->current_success_rate < 50) {
                if (recommended < NX_ASF_SF_MAX) {
                    recommended++;
                }
            }
            break;
            
        case NX_ASF_STRATEGY_ADAPTIVE:
            if (duty_cycle_used < asf->target_airtime_percent / 2) {
                if (asf->current_success_rate < 98) {
                    if (recommended < NX_ASF_SF_MAX) {
                        recommended++;
                    }
                }
            } else if (duty_cycle_used > asf->target_airtime_percent * 0.9) {
                if (asf->current_success_rate > NX_ASF_SUCCESS_HIGH) {
                    if (recommended > NX_ASF_SF_MIN) {
                        recommended--;
                    }
                }
            }
            /* fallthrough */
            
        case NX_ASF_STRATEGY_BALANCED:
        default:
            if (asf->current_success_rate < NX_ASF_SUCCESS_LOW) {
                if (recommended < NX_ASF_SF_MAX) {
                    recommended++;
                }
            } else if (asf->current_success_rate > NX_ASF_SUCCESS_HIGH &&
                       asf->avg_rssi > NX_ASF_RSSI_STRONG &&
                       asf->avg_snr > NX_ASF_SNR_GOOD) {
                if (recommended > NX_ASF_SF_MIN) {
                    recommended--;
                }
            } else if (asf->avg_rssi < NX_ASF_RSSI_WEAK ||
                       asf->avg_snr < NX_ASF_SNR_POOR) {
                if (recommended < NX_ASF_SF_MAX) {
                    recommended++;
                }
            }
            break;
    }
    
    recommended = NX_CLAMP(recommended, asf->min_sf, asf->max_sf);
    
    return recommended;
}

uint8_t nx_asf_get_recommended_sf(nx_asf_state_t *asf)
{
    if (!asf) return NX_ASF_SF_MIN;
    
    uint64_t now = nx_platform_time_ms();
    
    if ((now - asf->last_update_ms) >= asf->update_interval_ms) {
        uint8_t new_sf = nx_asf_calculate_recommended_sf(asf);
        
        asf->current_sf = new_sf;
        asf->last_update_ms = now;
        
        asf->window_start_ms = now;
        asf->current_airtime_ms = 0;
    }
    
    return asf->current_sf;
}

void nx_asf_set_bounds(nx_asf_state_t *asf, uint8_t min_sf, uint8_t max_sf)
{
    if (!asf) return;
    
    asf->min_sf = NX_CLAMP(min_sf, NX_ASF_SF_MIN, NX_ASF_SF_MAX);
    asf->max_sf = NX_CLAMP(max_sf, NX_ASF_SF_MIN, NX_ASF_SF_MAX);
    
    if (asf->min_sf > asf->max_sf) {
        asf->min_sf = asf->max_sf;
    }
    
    asf->current_sf = NX_CLAMP(asf->current_sf, asf->min_sf, asf->max_sf);
}

void nx_asf_set_strategy(nx_asf_state_t *asf, nx_asf_strategy_t strategy)
{
    if (!asf) return;
    asf->strategy = strategy;
}

void nx_asf_set_target_airtime(nx_asf_state_t *asf, uint32_t percent)
{
    if (!asf) return;
    asf->target_airtime_percent = NX_CLAMP(percent, 1, 100);
}

nx_asf_strategy_t nx_asf_get_strategy(const nx_asf_state_t *asf)
{
    if (!asf) return NX_ASF_STRATEGY_BALANCED;
    return asf->strategy;
}

uint8_t nx_asf_get_current_sf(const nx_asf_state_t *asf)
{
    if (!asf) return NX_ASF_SF_MIN;
    return asf->current_sf;
}

void nx_asf_get_stats(nx_asf_state_t *asf, uint32_t *sent, uint32_t *acked,
                      uint32_t *lost, uint8_t *success_rate)
{
    if (!asf) return;
    
    if (sent) *sent = asf->packets_sent;
    if (acked) *acked = asf->packets_acked;
    if (lost) *lost = asf->packets_lost;
    if (success_rate) *success_rate = asf->current_success_rate;
}

void nx_asf_get_link_quality(nx_asf_state_t *asf, int8_t *rssi, int8_t *snr)
{
    if (!asf) return;
    
    if (rssi) *rssi = asf->avg_rssi;
    if (snr) *snr = asf->avg_snr;
}

uint32_t nx_asf_estimate_airtime(uint8_t sf, size_t payload_len)
{
    if (sf < NX_ASF_SF_MIN || sf > NX_ASF_SF_MAX) {
        sf = NX_ASF_SF_MIN;
    }
    
    uint32_t base_airtime = nx_asf_packet_airtime_ms[sf];
    uint32_t payload_airtime = nx_asf_airtime_per_byte_ms[sf] * payload_len;
    
    return base_airtime + payload_airtime;
}

void nx_asf_force_sf(nx_asf_state_t *asf, uint8_t sf)
{
    if (!asf) return;
    asf->current_sf = NX_CLAMP(sf, asf->min_sf, asf->max_sf);
}

void nx_asf_reset(nx_asf_state_t *asf)
{
    if (!asf) return;
    
    asf->history_index = 0;
    asf->history_count = 0;
    memset(asf->history, 0, sizeof(asf->history));
    
    asf->packets_sent = 0;
    asf->packets_acked = 0;
    asf->packets_lost = 0;
    
    asf->avg_rssi = 0;
    asf->avg_snr = 0;
    asf->current_success_rate = 0;
    
    asf->current_airtime_ms = 0;
    asf->window_start_ms = nx_platform_time_ms();
    
    asf->current_sf = NX_ASF_SF_MAX;
}

const char* nx_asf_strategy_name(nx_asf_strategy_t strategy)
{
    switch (strategy) {
        case NX_ASF_STRATEGY_CONSERVATIVE: return "conservative";
        case NX_ASF_STRATEGY_BALANCED:     return "balanced";
        case NX_ASF_STRATEGY_AGGRESSIVE:    return "aggressive";
        case NX_ASF_STRATEGY_ADAPTIVE:      return "adaptive";
        default: return "unknown";
    }
}
