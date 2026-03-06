/*
 * NEXUS Protocol -- WiFi HaLow (802.11ah) Transport Header
 */

#ifndef NEXUS_HALOW_H
#define NEXUS_HALOW_H

#include "transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HaLow configuration */
typedef struct {
    uint8_t  channel;
    uint8_t  bandwidth;
    uint8_t  rate;
    int8_t   tx_power_dbm;
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  password[64];
    bool     ap_mode;
    uint8_t  ps_mode;
    uint8_t  security;
    bool     mesh_enabled;
    uint8_t  mesh_id[32];
    char     ifname[16];
} nx_halow_config_t;

/* Power save modes */
#define NX_HALOW_PS_ACTIVE   0
#define NX_HALOW_PS_WMM      1
#define NX_HALOW_PS_TIM      2
#define NX_HALOW_PS_NON_TIM  3

/* Security modes */
#define NX_HALOW_SEC_OPEN      0
#define NX_HALOW_SEC_WPA3_SAE  1
#define NX_HALOW_SEC_WPA2_PSK  2
#define NX_HALOW_SEC_OWE       3

/* Create HaLow transport */
nx_transport_t *nx_halow_transport_create(void);

/* Metrics */
nx_err_t nx_halow_get_metrics(nx_transport_t *t, int8_t *rssi_out,
                                uint8_t *snr_out, uint8_t *quality_out);

/* Statistics */
nx_err_t nx_halow_get_stats(nx_transport_t *t, uint64_t *tx_packets,
                            uint64_t *tx_bytes, uint64_t *rx_packets,
                            uint64_t *rx_bytes, uint64_t *tx_errors,
                            uint64_t *rx_errors);

/* Reconfigure */
nx_err_t nx_halow_reconfigure(nx_transport_t *t, uint8_t new_channel,
                             uint8_t new_bandwidth, uint8_t new_rate);

/* Airtime estimation */
uint32_t nx_halow_estimate_airtime(size_t payload_len, uint8_t rate);

/* Channel assessment */
nx_err_t nx_halow_assess_channel(nx_transport_t *t, uint8_t *out_rate,
                                  uint8_t *out_power);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_HALOW_H */
