/*
 * NEXUS Protocol -- WiFi HaLow (802.11ah) Transport
 */

#define _POSIX_C_SOURCE 200809L

#include "nexus/halow.h"
#include "nexus/platform.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef __linux__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <net/if.h>
    #include <sys/ioctl.h>
    #include <linux/if_packet.h>
    #include <linux/if_ether.h>
#endif

/* HaLow defaults */
#define NX_HALOW_CHAN_1     1
#define NX_HALOW_CHAN_6     6
#define NX_HALOW_CHAN_11    11
#define NX_HALOW_DEFAULT_CHAN NX_HALOW_CHAN_6

/* Data rates */
#define NX_HALOW_RATE_150K  0
#define NX_HALOW_RATE_300K  1
#define NX_HALOW_RATE_600K  2
#define NX_HALOW_DEFAULT_RATE NX_HALOW_RATE_300K

/* Max payload */
#define NX_HALOW_MAX_PAYLOAD 2047
#define NX_HALOW_MTU         1500

/* State structure - using uint8_t for enums to avoid conflicts */
typedef struct {
    nx_halow_config_t config;
    int                 sock_fd;
    bool                initialized;
    bool                connected;
    int8_t              rssi;
    uint8_t             snr;
    uint8_t             link_quality;
    uint64_t            tx_packets;
    uint64_t            tx_bytes;
    uint64_t            rx_packets;
    uint64_t            rx_bytes;
    uint64_t            tx_errors;
    uint64_t            rx_errors;
    uint8_t             rx_buf[NX_HALOW_MAX_PAYLOAD];
    size_t              rx_len;
    bool                rx_pending;
} nx_halow_state_t;

/* Default config */
static const nx_halow_config_t nx_halow_default_config = {
    .channel = NX_HALOW_DEFAULT_CHAN,
    .bandwidth = 1,
    .rate = NX_HALOW_DEFAULT_RATE,
    .tx_power_dbm = 20,
    .ssid = {0},
    .ssid_len = 0,
    .password = {0},
    .ap_mode = false,
    .ps_mode = NX_HALOW_PS_ACTIVE,
    .security = NX_HALOW_SEC_WPA3_SAE,
    .mesh_enabled = true,
    .mesh_id = {'N', 'E', 'X', 'U', 'S', 0},
    .ifname = {'h', 'a', 'l', 'o', 'w', '0', 0},
};

/* Forward declarations */
static nx_err_t halow_init(nx_transport_t *t, const void *config);
static nx_err_t halow_send(nx_transport_t *t, const uint8_t *data, size_t len);
static nx_err_t halow_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms);
static void     halow_destroy(nx_transport_t *t);

/* Transport operations */
static const nx_transport_ops_t nx_halow_ops = {
    .init    = halow_init,
    .send    = halow_send,
    .recv    = halow_recv,
    .destroy = halow_destroy,
};

static nx_err_t halow_init(nx_transport_t *t, const void *config)
{
    nx_halow_state_t *state;
    const nx_halow_config_t *cfg = config ? config : &nx_halow_default_config;
    
    if (!t) return NX_ERR_INVALID_ARG;
    
    state = (nx_halow_state_t *)malloc(sizeof(nx_halow_state_t));
    if (!state) return NX_ERR_NO_MEMORY;
    
    memset(state, 0, sizeof(*state));
    memcpy(&state->config, cfg, sizeof(nx_halow_config_t));
    
#ifdef __linux__
    state->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (state->sock_fd >= 0) {
        int flags = fcntl(state->sock_fd, F_GETFL, 0);
        fcntl(state->sock_fd, F_SETFL, flags | O_NONBLOCK);
    }
    /* sock_fd < 0 is tolerated -- operates in stub mode without hardware */
#else
    state->sock_fd = -1;
#endif
    
    state->initialized = true;
    state->connected = cfg->mesh_enabled;
    
    t->state = state;
    t->type = NX_TRANSPORT_WIFI;
    t->active = true;
    
    state->link_quality = 128;
    
    return NX_OK;
}

static nx_err_t halow_send(nx_transport_t *t, const uint8_t *data, size_t len)
{
    nx_halow_state_t *state;
    ssize_t sent;
    
    if (!t || !t->state || !data) return NX_ERR_INVALID_ARG;
    if (len > NX_HALOW_MAX_PAYLOAD) return NX_ERR_BUFFER_TOO_SMALL;
    
    state = (nx_halow_state_t *)t->state;
    if (!state->initialized) return NX_ERR_TRANSPORT;
    if (!state->connected && !state->config.mesh_enabled) {
        return NX_ERR_TRANSPORT;
    }
    
    (void)sent;
    
#ifdef __linux__
    if (state->sock_fd >= 0) {
        /* STUB: Would construct and send 802.11 frame */
        sent = (ssize_t)len;
    } else {
        /* Stub mode -- no real socket, simulate success */
        sent = (ssize_t)len;
    }
#else
    sent = (ssize_t)len;
#endif
    
    if (sent < 0) {
        state->tx_errors++;
        return NX_ERR_TRANSPORT;
    }
    
    state->tx_packets++;
    state->tx_bytes += len;
    
    return NX_OK;
}

static nx_err_t halow_recv(nx_transport_t *t, uint8_t *buf, size_t buf_len,
                           size_t *out_len, uint32_t timeout_ms)
{
    nx_halow_state_t *state;
    ssize_t received;
    uint64_t start_time, now;
    
    if (!t || !t->state || !buf || !out_len) return NX_ERR_INVALID_ARG;
    
    state = (nx_halow_state_t *)t->state;
    if (!state->initialized) return NX_ERR_TRANSPORT;
    
    start_time = nx_platform_time_ms();
    
    while (1) {
        if (state->rx_pending && state->rx_len <= buf_len) {
            memcpy(buf, state->rx_buf, state->rx_len);
            *out_len = state->rx_len;
            state->rx_pending = false;
            state->rx_len = 0;
            
            state->rx_packets++;
            state->rx_bytes += *out_len;
            return NX_OK;
        }
        
#ifdef __linux__
        if (state->sock_fd >= 0) {
            received = 0;
            (void)received;
        } else {
            received = 0;
        }
#else
        received = 0;
#endif
        
        now = nx_platform_time_ms();
        if (timeout_ms > 0 && (now - start_time) >= timeout_ms) {
            return NX_ERR_TIMEOUT;
        }
        
        /* Brief yield - use nanosleep with proper timespec */
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000; /* 1ms */
        nanosleep(&ts, NULL);
    }
}

static void halow_destroy(nx_transport_t *t)
{
    nx_halow_state_t *state;
    
    if (!t || !t->state) return;
    
    state = (nx_halow_state_t *)t->state;
    
#ifdef __linux__
    if (state->sock_fd >= 0) {
        close(state->sock_fd);
        state->sock_fd = -1;
    }
#endif
    
    if (state->config.password[0]) {
        /* Wipe password */
        volatile unsigned char *p = state->config.password;
        size_t n = sizeof(state->config.password);
        while (n--) *p++ = 0;
    }
    
    free(state);
    t->state = NULL;
    t->active = false;
}

/* Public API */

nx_transport_t *nx_halow_transport_create(void)
{
    nx_transport_t *t = (nx_transport_t *)malloc(sizeof(nx_transport_t));
    if (!t) return NULL;
    
    memset(t, 0, sizeof(*t));
    t->type = NX_TRANSPORT_WIFI;
    t->name = "halow";
    t->ops = &nx_halow_ops;
    t->active = false;
    
    return t;
}

nx_err_t nx_halow_get_metrics(nx_transport_t *t, int8_t *rssi_out,
                                uint8_t *snr_out, uint8_t *quality_out)
{
    nx_halow_state_t *state;
    
    if (!t || !t->state) return NX_ERR_INVALID_ARG;
    
    state = (nx_halow_state_t *)t->state;
    
    if (rssi_out) *rssi_out = state->rssi;
    if (snr_out) *snr_out = state->snr;
    if (quality_out) *quality_out = state->link_quality;
    
    return NX_OK;
}

nx_err_t nx_halow_get_stats(nx_transport_t *t, uint64_t *tx_packets,
                            uint64_t *tx_bytes, uint64_t *rx_packets,
                            uint64_t *rx_bytes, uint64_t *tx_errors,
                            uint64_t *rx_errors)
{
    nx_halow_state_t *state;
    
    if (!t || !t->state) return NX_ERR_INVALID_ARG;
    
    state = (nx_halow_state_t *)t->state;
    
    if (tx_packets) *tx_packets = state->tx_packets;
    if (tx_bytes) *tx_bytes = state->tx_bytes;
    if (rx_packets) *rx_packets = state->rx_packets;
    if (rx_bytes) *rx_bytes = state->rx_bytes;
    if (tx_errors) *tx_errors = state->tx_errors;
    if (rx_errors) *rx_errors = state->rx_errors;
    
    return NX_OK;
}

nx_err_t nx_halow_reconfigure(nx_transport_t *t, uint8_t new_channel,
                             uint8_t new_bandwidth, uint8_t new_rate)
{
    nx_halow_state_t *state;
    
    if (!t || !t->state) return NX_ERR_INVALID_ARG;
    if (new_channel < 1 || new_channel > 14) return NX_ERR_INVALID_ARG;
    if (new_bandwidth != 1 && new_bandwidth != 2 && new_bandwidth != 4) {
        return NX_ERR_INVALID_ARG;
    }
    
    state = (nx_halow_state_t *)t->state;
    
    state->config.channel = new_channel;
    state->config.bandwidth = new_bandwidth;
    state->config.rate = new_rate;
    
    return NX_OK;
}

uint32_t nx_halow_estimate_airtime(size_t payload_len, uint8_t rate)
{
    uint32_t symbol_duration_us;
    uint32_t symbols;
    uint32_t preamble_us = 560;
    
    switch (rate) {
        case NX_HALOW_RATE_150K:
            symbol_duration_us = 40;
            symbols = (payload_len * 8 + 23) / 24;
            break;
        case NX_HALOW_RATE_300K:
            symbol_duration_us = 40;
            symbols = (payload_len * 8 + 39) / 40;
            break;
        case NX_HALOW_RATE_600K:
            symbol_duration_us = 40;
            symbols = (payload_len * 8 + 63) / 64;
            break;
        default:
            return 0;
    }
    
    symbols += 8;
    return preamble_us + (symbols * symbol_duration_us) + 100;
}

nx_err_t nx_halow_assess_channel(nx_transport_t *t, uint8_t *out_rate,
                                  uint8_t *out_power)
{
    nx_halow_state_t *state;
    
    if (!t || !t->state || !out_rate) return NX_ERR_INVALID_ARG;
    
    state = (nx_halow_state_t *)t->state;
    
    if (state->snr > 20) {
        *out_rate = NX_HALOW_RATE_600K;
        *out_power = 14;
    } else if (state->snr > 10) {
        *out_rate = NX_HALOW_RATE_300K;
        *out_power = 17;
    } else {
        *out_rate = NX_HALOW_RATE_150K;
        *out_power = 20;
    }
    
    return NX_OK;
}
