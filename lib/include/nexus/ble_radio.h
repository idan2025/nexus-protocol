/*
 * NEXUS Protocol -- BLE Radio Hardware Abstraction Layer
 *
 * Pure C abstraction over BLE radio hardware.
 * Each BLE chip provides an implementation of this vtable.
 * Desktop builds can use a mock radio for testing.
 *
 * BLE GATT characteristic-based transport:
 *   - Peripheral mode: advertise + accept connections
 *   - Central mode: scan + connect
 *   - ATT MTU negotiated (default 247 for NX_MAX_PACKET)
 *   - PDUs are self-delimiting (no framing needed)
 */
#ifndef NEXUS_BLE_RADIO_H
#define NEXUS_BLE_RADIO_H

#include "types.h"

/* ── BLE Configuration ────────────────────────────────────────────────── */

typedef struct {
    bool     peripheral;    /* true=peripheral/advertise, false=central/scan */
    uint16_t mtu;           /* Desired ATT MTU (default 247 for NX_MAX_PACKET) */
} nx_ble_config_t;

#define NX_BLE_CONFIG_DEFAULT { .peripheral = true, .mtu = 247 }

/* ── BLE State ────────────────────────────────────────────────────────── */

typedef enum {
    NX_BLE_IDLE,
    NX_BLE_ADVERTISING,
    NX_BLE_SCANNING,
    NX_BLE_CONNECTED,
} nx_ble_state_t;

/* ── BLE Radio HAL vtable ─────────────────────────────────────────────── */

typedef struct nx_ble_radio nx_ble_radio_t;

typedef struct {
    /* Initialize the radio with given config. */
    nx_err_t (*init)(nx_ble_radio_t *radio, const nx_ble_config_t *config);

    /* Send a packet. Blocks until TX complete or error. */
    nx_err_t (*send)(nx_ble_radio_t *radio, const uint8_t *data, size_t len);

    /* Receive a packet. Blocks up to timeout_ms.
     * Returns NX_ERR_TIMEOUT if nothing received. */
    nx_err_t (*recv)(nx_ble_radio_t *radio, uint8_t *buf, size_t buf_len,
                     size_t *out_len, uint32_t timeout_ms);

    /* Set radio to sleep (lowest power). */
    nx_err_t (*sleep)(nx_ble_radio_t *radio);

    /* Destroy / free radio resources. */
    void     (*destroy)(nx_ble_radio_t *radio);
} nx_ble_radio_ops_t;

struct nx_ble_radio {
    const nx_ble_radio_ops_t *ops;
    nx_ble_config_t           config;
    nx_ble_state_t            state;
    void                     *hw;    /* Hardware-specific context */
};

/* ── Mock Radio (for desktop testing) ─────────────────────────────────── */

/*
 * Create a mock BLE radio that operates in-memory.
 * Two mock radios can be linked together for loopback testing.
 */
nx_ble_radio_t *nx_ble_mock_create(void);

/*
 * Link two mock BLE radios so send on one delivers to the other.
 * Must be called after both are created and before use.
 */
void nx_ble_mock_link(nx_ble_radio_t *a, nx_ble_radio_t *b);

#endif /* NEXUS_BLE_RADIO_H */
