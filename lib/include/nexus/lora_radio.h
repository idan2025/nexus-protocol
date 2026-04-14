/*
 * NEXUS Protocol -- LoRa Radio Hardware Abstraction Layer
 *
 * Pure C abstraction over LoRa radio hardware (SX1262, SX1276, etc.).
 * Each radio chip provides an implementation of this vtable.
 * Desktop builds can use a mock radio for testing.
 *
 * Typical SX1262 parameters for off-grid mesh:
 *   Frequency: 915 MHz (US) / 868 MHz (EU)
 *   Bandwidth: 125 kHz or 250 kHz
 *   Spreading Factor: 7-12 (adaptive)
 *   Coding Rate: 4/5 to 4/8
 *   TX Power: 2-22 dBm
 *   Sync Word: 0x12 (private), 0x34 (public LoRaWAN)
 */
#ifndef NEXUS_LORA_RADIO_H
#define NEXUS_LORA_RADIO_H

#include "types.h"

/* ── LoRa Configuration ─────────────────────────────────────────────── */

typedef struct {
    uint32_t frequency_hz;     /* e.g., 915000000 */
    uint32_t bandwidth_hz;     /* 125000, 250000, 500000 */
    uint8_t  spreading_factor; /* 7-12 */
    uint8_t  coding_rate;      /* 5-8 (denominator of 4/x) */
    int8_t   tx_power_dbm;     /* 2-22 for SX1262 */
    uint16_t preamble_len;     /* Preamble symbols (8 typical) */
    uint8_t  sync_word;        /* 0x12 private, 0x34 public */
    bool     crc_on;           /* Hardware CRC */
    bool     implicit_header;  /* Implicit (fixed-len) vs explicit header */
    float    tcxo_voltage;     /* TCXO voltage via DIO3 (0=no TCXO, 1.6 RAK4631, 1.8 WIO-SX1262) */
} nx_lora_config_t;

/* ── Regional default frequency ──────────────────────────────────────
 *
 * Pass -DNX_LORA_REGION=<tag> at build time to set the default frequency
 * for this build. Tags match Meshtastic region names so users and
 * firmware targets stay aligned. Channel choice is the Meshtastic "LongFast"
 * mid-slot where applicable, which avoids the low/high edges where LoRaWAN
 * gateways tend to listen.
 *
 * Users can still override at runtime via BLE config / nexus-cli.
 *   US915 (default), EU868, EU433, CN470, AS923, IN865, AU915, KR920, TH923
 */
#if defined(NX_LORA_REGION_EU868)
  #define NX_LORA_DEFAULT_FREQ_HZ  869525000u
#elif defined(NX_LORA_REGION_EU433)
  #define NX_LORA_DEFAULT_FREQ_HZ  433175000u
#elif defined(NX_LORA_REGION_CN470)
  #define NX_LORA_DEFAULT_FREQ_HZ  470000000u
#elif defined(NX_LORA_REGION_AS923)
  #define NX_LORA_DEFAULT_FREQ_HZ  923625000u
#elif defined(NX_LORA_REGION_IN865)
  #define NX_LORA_DEFAULT_FREQ_HZ  866375000u
#elif defined(NX_LORA_REGION_AU915)
  #define NX_LORA_DEFAULT_FREQ_HZ  917625000u
#elif defined(NX_LORA_REGION_KR920)
  #define NX_LORA_DEFAULT_FREQ_HZ  921875000u
#elif defined(NX_LORA_REGION_TH923)
  #define NX_LORA_DEFAULT_FREQ_HZ  923125000u
#else
  /* NX_LORA_REGION_US915 -- default */
  #define NX_LORA_DEFAULT_FREQ_HZ  906875000u
#endif

/* Sensible defaults for NEXUS mesh */
#define NX_LORA_CONFIG_DEFAULT { \
    .frequency_hz     = NX_LORA_DEFAULT_FREQ_HZ, \
    .bandwidth_hz     = 250000,    \
    .spreading_factor = 9,         \
    .coding_rate      = 5,         \
    .tx_power_dbm     = 17,        \
    .preamble_len     = 8,         \
    .sync_word        = 0x12,      \
    .crc_on           = true,      \
    .implicit_header  = false,     \
    .tcxo_voltage     = 0.0f,      \
}

/* ── Radio State ─────────────────────────────────────────────────────── */

typedef enum {
    NX_RADIO_SLEEP,
    NX_RADIO_STANDBY,
    NX_RADIO_RX,
    NX_RADIO_TX,
    NX_RADIO_CAD,       /* Channel Activity Detection */
} nx_radio_state_t;

/* ── Receive Info ────────────────────────────────────────────────────── */

typedef struct {
    int16_t  rssi;        /* dBm */
    int8_t   snr;         /* dB (x4 on SX1262) */
    uint32_t freq_error;  /* Hz */
} nx_lora_rx_info_t;

/* ── Radio HAL vtable ────────────────────────────────────────────────── */

typedef struct nx_lora_radio nx_lora_radio_t;

typedef struct {
    /* Initialize the radio with given config. */
    nx_err_t (*init)(nx_lora_radio_t *radio, const nx_lora_config_t *config);

    /* Transmit a packet. Blocks until TX complete or error. */
    nx_err_t (*transmit)(nx_lora_radio_t *radio,
                         const uint8_t *data, size_t len);

    /* Receive a packet. Blocks up to timeout_ms.
     * Returns NX_ERR_TIMEOUT if nothing received.
     * On success, fills buf, *out_len, and *rx_info. */
    nx_err_t (*receive)(nx_lora_radio_t *radio,
                        uint8_t *buf, size_t buf_len,
                        size_t *out_len, nx_lora_rx_info_t *rx_info,
                        uint32_t timeout_ms);

    /* Check for channel activity (CAD). Returns true if signal detected. */
    nx_err_t (*cad)(nx_lora_radio_t *radio, bool *activity);

    /* Set radio to sleep (lowest power). */
    nx_err_t (*sleep)(nx_lora_radio_t *radio);

    /* Set radio to standby. */
    nx_err_t (*standby)(nx_lora_radio_t *radio);

    /* Reconfigure radio parameters (frequency, SF, etc.). */
    nx_err_t (*reconfigure)(nx_lora_radio_t *radio,
                            const nx_lora_config_t *config);

    /* Destroy / free radio resources. */
    void (*destroy)(nx_lora_radio_t *radio);
} nx_lora_radio_ops_t;

struct nx_lora_radio {
    const nx_lora_radio_ops_t *ops;
    nx_lora_config_t           config;
    nx_radio_state_t           state;
    void                      *hw;    /* Hardware-specific context */
};

/* ── Airtime Calculation ─────────────────────────────────────────────── */

/*
 * Calculate on-air time for a LoRa packet in milliseconds.
 * Useful for duty cycle budgeting and collision avoidance.
 */
uint32_t nx_lora_airtime_ms(const nx_lora_config_t *config, size_t payload_len);

/* ── Mock Radio (for desktop testing) ────────────────────────────────── */

/*
 * Create a mock radio that operates in-memory.
 * Two mock radios can be linked together for loopback testing.
 */
nx_lora_radio_t *nx_lora_mock_create(void);

/*
 * Link two mock radios so transmit on one delivers to the other.
 * Must be called after both are created and before use.
 */
void nx_lora_mock_link(nx_lora_radio_t *a, nx_lora_radio_t *b);

#endif /* NEXUS_LORA_RADIO_H */
