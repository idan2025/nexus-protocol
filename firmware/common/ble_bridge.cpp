/*
 * NEXUS Firmware -- BLE Bridge (ESP32 BLE implementation)
 *
 * Uses ESP32 NimBLE stack via Arduino BLE library.
 * Implements Nordic UART Service (NUS) for packet tunneling.
 */
#ifdef NX_PLATFORM_ESP32

#include "ble_bridge.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

/* NUS UUIDs (Nordic UART Service -- widely supported by phone apps) */
#define NUS_SERVICE_UUID    "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID         "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID         "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

/* Receive buffer: ring of packets from phone */
#define BLE_RX_SLOTS   8
#define BLE_MAX_PKT    260  /* 2-byte length header + 258 max */

typedef struct {
    uint8_t  data[BLE_MAX_PKT];
    size_t   len;
    bool     valid;
} ble_rx_slot_t;

static ble_rx_slot_t rx_ring[BLE_RX_SLOTS];
static volatile uint8_t rx_write = 0;
static volatile uint8_t rx_read  = 0;

static NimBLEServer *ble_server = nullptr;
static NimBLECharacteristic *tx_char = nullptr;
static NimBLECharacteristic *rx_char = nullptr;
static bool client_connected = false;

/* ── BLE Callbacks ────────────────────────────────────────────────────── */

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
        (void)s;
        (void)info;
        client_connected = true;
        Serial.println("[BLE] Client connected");
        /* Allow multiple connections / update connection params */
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
        (void)s;
        (void)info;
        (void)reason;
        client_connected = false;
        Serial.println("[BLE] Client disconnected");
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
        (void)info;
        NimBLEAttValue val = c->getValue();
        size_t len = val.length();
        const uint8_t *data = val.data();

        if (len < 3) return;  /* Need at least 2-byte header + 1 byte */

        uint16_t pkt_len = ((uint16_t)data[0] << 8) | data[1];
        if (pkt_len + 2 > len || pkt_len > NX_MAX_PACKET) return;

        /* Store in ring buffer */
        uint8_t slot = rx_write % BLE_RX_SLOTS;
        if (!rx_ring[slot].valid) {
            memcpy(rx_ring[slot].data, &data[2], pkt_len);
            rx_ring[slot].len = pkt_len;
            rx_ring[slot].valid = true;
            rx_write++;
        }
        /* else: ring full, drop packet */
    }
};

static ServerCallbacks server_cb;
static RxCallbacks rx_cb;

/* ── Public API ───────────────────────────────────────────────────────── */

extern "C" {

void nx_ble_bridge_init(const char *device_name)
{
    memset(rx_ring, 0, sizeof(rx_ring));
    rx_write = 0;
    rx_read = 0;

    NimBLEDevice::init(device_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);  /* Request large MTU */

    ble_server = NimBLEDevice::createServer();
    ble_server->setCallbacks(&server_cb);

    NimBLEService *nus = ble_server->createService(NUS_SERVICE_UUID);

    /* TX: firmware -> phone (notify) */
    tx_char = nus->createCharacteristic(
        NUS_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    /* RX: phone -> firmware (write) */
    rx_char = nus->createCharacteristic(
        NUS_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rx_char->setCallbacks(&rx_cb);

    nus->start();

    Serial.printf("[BLE] Bridge initialized: %s\n", device_name);
}

void nx_ble_bridge_start(void)
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setName(NimBLEDevice::toString().c_str());
    adv->start();
    Serial.println("[BLE] Advertising started");
}

void nx_ble_bridge_stop(void)
{
    NimBLEDevice::getAdvertising()->stop();
}

bool nx_ble_bridge_connected(void)
{
    return client_connected;
}

nx_err_t nx_ble_bridge_send(const uint8_t *data, size_t len)
{
    if (!client_connected || !tx_char) return NX_ERR_TRANSPORT;
    if (len > NX_MAX_PACKET) return NX_ERR_BUFFER_TOO_SMALL;

    /* Frame: [LEN_HI][LEN_LO][data...] */
    uint8_t frame[BLE_MAX_PKT];
    frame[0] = (uint8_t)(len >> 8);
    frame[1] = (uint8_t)(len & 0xFF);
    memcpy(&frame[2], data, len);

    tx_char->setValue(frame, len + 2);
    tx_char->notify();

    return NX_OK;
}

nx_err_t nx_ble_bridge_recv(uint8_t *buf, size_t buf_len, size_t *out_len)
{
    uint8_t slot = rx_read % BLE_RX_SLOTS;
    if (!rx_ring[slot].valid) {
        *out_len = 0;
        return NX_ERR_TIMEOUT;
    }

    size_t copy_len = rx_ring[slot].len;
    if (copy_len > buf_len) copy_len = buf_len;

    memcpy(buf, rx_ring[slot].data, copy_len);
    *out_len = copy_len;

    rx_ring[slot].valid = false;
    rx_read++;

    return NX_OK;
}

void nx_ble_bridge_poll(void)
{
    /* NimBLE handles events in its own FreeRTOS task,
     * nothing to do here for ESP32. */
}

} /* extern "C" */

#endif /* NX_PLATFORM_ESP32 */
