# NEXUS Protocol

**Network EXchange Unified System** -- E2E encrypted, multi-transport mesh networking protocol.

NEXUS is a **Reticulum-style** mesh protocol: every device (phone, microcontroller, Raspberry Pi) runs a full protocol node with its own identity, routing, and cryptography. No device is "dumb" -- an ESP32 operates as both an independent mesh relay and a radio bridge for your phone.

## Features

- **End-to-end encryption** -- XChaCha20-Poly1305 AEAD, Ed25519 signatures, X25519 key exchange
- **Forward secrecy** -- Signal-style Double Ratchet sessions with per-message key derivation
- **Group encryption** -- Sender-key chain ratchet for efficient group messaging
- **Multi-transport** -- LoRa, BLE, WiFi HaLow, TCP, Serial, in-memory pipes
- **PRISM routing** -- Hybrid proactive/reactive routing with 32 neighbors, 64 routes
- **Fragmentation** -- Up to 3.8 KB messages across constrained radio links (16 fragments)
- **Store-and-forward** -- ANCHOR nodes hold messages for offline peers (32 slots, 1hr TTL)
- **Gateway bridging** -- Cross-transport forwarding (e.g., BLE to LoRa)
- **Adaptive spreading factor** -- Dynamic LoRa SF7-SF12 based on link quality
- **Tiny footprint** -- Pure C99, ~77 KB RAM, runs on ESP32-S3 and nRF52840

## Architecture

```
┌──────────────┐  BLE   ┌──────────────┐  LoRa   ┌──────────────┐
│  Android App │◄──────►│   ESP32 +    │◄───────►│  Other nodes │
│  (NEXUS node │        │   SX1262     │         │  (ESP32/nRF/ │
│   via JNI)   │        │ (NEXUS node) │         │   phone/RPi) │
└──────────────┘        └──────────────┘         └──────────────┘
```

Each device runs a full NEXUS node. The phone talks to the ESP32 over BLE, which extends the mesh to LoRa range. All communication is end-to-end encrypted.

## Quick Start

### Build the C library + tests

```sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Run Python binding tests

```sh
cd bindings/python/tests
PYTHONPATH=.. python3 -m unittest discover -v
```

### Python CLI

```sh
cd bindings/python
python3 cli.py identity generate
python3 cli.py identity save mynode.key
python3 cli.py identity show mynode.key
```

### Flash firmware (requires PlatformIO)

```sh
# Heltec WiFi LoRa 32 V3
cd firmware/heltec_v3 && pio run -t upload

# RAK4631 WisBlock Core
cd firmware/rak4631 && pio run -t upload
```

### Build Android app (requires Android Studio + NDK)

```sh
cd android && ./gradlew assembleDebug
```

## Cryptography

| Primitive | Algorithm | Purpose |
|-----------|-----------|---------|
| AEAD | XChaCha20-Poly1305 | Packet encryption |
| Signatures | Ed25519 | Identity announcements |
| Key exchange | X25519 | Ephemeral + session DH |
| Hashing | BLAKE2b | Address derivation, KDF |
| Sessions | Double Ratchet | Forward secrecy |
| Groups | Sender-key chain | Efficient group crypto |

All cryptography provided by [Monocypher](https://monocypher.org/) 4.0.2 (vendored, zero dependencies).

## Packet Format

13-byte compact header, big-endian on wire:

```
[version(4b)][flags(4b)][ptype(4b)][rtype(4b)][hop(4b)][ttl(4b)]
[src_short(4B)][dst_short(4B)][seq(1B)][payload_len(1B)]
```

- Addresses: 16-byte full (BLAKE2b-128 of Ed25519 pubkey), 4-byte short on wire
- Ephemeral payload: `[eph_pub(32)][nonce(24)][MAC(16)][ciphertext]` = 72B overhead
- Session payload: `[msg_num(4)][prev_n(4)][DH_pub(32)][nonce(24)][MAC(16)][ct]` = 80B overhead

## Project Structure

```
nexus/
├── lib/                    # Core C library (libnexus)
│   ├── include/nexus/      # Public headers
│   ├── src/                # Implementation
│   ├── test/               # 17 test suites
│   └── vendor/monocypher/  # Monocypher 4.0.2
├── transports/             # Transport implementations
│   ├── serial/             # UART (0x7E framing)
│   ├── tcp/                # TCP (0x7E framing)
│   ├── lora/               # LoRa + adaptive SF
│   ├── ble/                # BLE (GATT PDUs)
│   ├── wifi/               # WiFi HaLow (802.11ah)
│   └── pipe/               # In-memory (testing)
├── bindings/python/        # Python ctypes bindings + CLI
├── firmware/               # MCU firmware (PlatformIO)
│   ├── heltec_v3/          # Heltec WiFi LoRa 32 V3
│   └── rak4631/            # RAK4631 WisBlock Core
└── android/                # Android app (Kotlin + Compose)
```

## Test Coverage

17 C test suites + 17 Python tests covering:

- Identity generation, address derivation, key uniqueness
- Packet serialization/deserialization, all flag combinations
- AEAD encryption, tamper detection, key agreement
- Ed25519 signed announcements
- PRISM routing: neighbors, routes, dedup, RREQ/RREP/RERR
- LoRa transport with mock radio, duty cycle, CAD
- BLE transport with mock radio
- Fragmentation and reassembly (up to 3.8 KB)
- Store-and-forward anchor mailbox
- Double Ratchet sessions with forward secrecy
- Group sender-key encryption
- Gateway cross-transport bridging
- WiFi HaLow transport
- Adaptive spreading factor
- 1024-node stress tests

## Supported Hardware

| Device | Role | Transport |
|--------|------|-----------|
| Heltec WiFi LoRa 32 V3 | ESP32-S3 + SX1262 | LoRa + BLE |
| RAK4631 WisBlock | nRF52840 + SX1262 | LoRa + BLE |
| Raspberry Pi | Linux gateway | TCP + Serial + BLE |
| Android phone | Mobile node | BLE |
| Any Linux box | Desktop node | TCP + Serial |

## License

MIT
