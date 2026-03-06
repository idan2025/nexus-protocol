/*
 * NEXUS Firmware -- BLE Bridge
 *
 * Tunnels NEXUS packets between a phone app and the firmware node
 * over BLE using the Nordic UART Service (NUS) profile.
 *
 * Protocol: [LEN_HI][LEN_LO][NEXUS_PACKET]
 * Max packet: 255 bytes (fits in BLE MTU with fragmentation)
 */
#ifndef NEXUS_BLE_BRIDGE_H
#define NEXUS_BLE_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "nexus/types.h"

/* Initialize BLE bridge. Call once in setup().
 * device_name: BLE advertised name (e.g., "NEXUS-A1B2") */
void nx_ble_bridge_init(const char *device_name);

/* Start BLE advertising. */
void nx_ble_bridge_start(void);

/* Stop BLE advertising. */
void nx_ble_bridge_stop(void);

/* Check if a phone is connected. */
bool nx_ble_bridge_connected(void);

/* Send a packet to the connected phone.
 * Called by firmware when a LoRa packet arrives that should be
 * forwarded to the phone app. */
nx_err_t nx_ble_bridge_send(const uint8_t *data, size_t len);

/* Receive a packet from the phone (non-blocking).
 * Returns NX_OK if a packet was available, NX_ERR_TIMEOUT if not. */
nx_err_t nx_ble_bridge_recv(uint8_t *buf, size_t buf_len,
                             size_t *out_len);

/* Poll BLE events. Call in loop(). */
void nx_ble_bridge_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_BLE_BRIDGE_H */
