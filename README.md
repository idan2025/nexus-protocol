# NEXUS Protocol

**Network EXchange Unified System** — end-to-end encrypted, multi-transport mesh networking.

NEXUS is a **Reticulum-style** mesh protocol: every device — phone, microcontroller, single-board computer, VPS — runs a full protocol node with its own identity, routing, and cryptography. No device is "dumb". An ESP32 is both an independent mesh node *and* a radio bridge for your phone; a Linux box can serve as a public internet relay (pillar) that lets phones on different networks reach each other without port-forwarding.

Current release: **v0.6.24**.

## Features

- **End-to-end encryption** — XChaCha20-Poly1305 AEAD, Ed25519 signatures, X25519 key exchange
- **Forward secrecy** — Signal-style Double Ratchet sessions with per-message key derivation
- **Group encryption** — Sender-key chain ratchet for efficient group messaging
- **Multi-transport** — LoRa, BLE, WiFi HaLow, TCP, UDP multicast, Serial, in-memory pipes
- **Zero-config LAN discovery** — UDP multicast on every interface; nodes find each other automatically
- **Public internet relays** — pillar nodes accept outbound TCP from phones behind NAT, no port-forwarding required on clients
- **PRISM routing** — Hybrid proactive/reactive routing, 32 neighbors, 64 routes, deduplication, RREQ/RREP/RERR
- **Fragmentation** — Up to 3.8 KB messages across constrained radio links (16 fragments × 238 B)
- **Store-and-forward** — Tiered mailbox: RELAY (30 min), ANCHOR (1 h), VAULT (24 h)
- **Adaptive LoRa** — Dynamic SF7–SF12 based on link quality, CAD collision avoidance, 1% duty-cycle enforcement
- **Gateway bridging** — Cross-transport forwarding (e.g., BLE ↔ LoRa, LoRa ↔ TCP)
- **NXM message envelope** — Structured payloads: text, file, image, location, voice, reaction, ACK, typing, read, delete
- **Tiny footprint** — Pure C99, ~77 KB RAM, runs on ESP32-S3 and nRF52840

## Architecture

```
            ┌────────────────────────────────────────────────────┐
            │              Public Internet (TCP)                 │
            └────────┬──────────────────────────────────┬────────┘
                     │                                  │
              ┌──────▼──────┐                    ┌──────▼──────┐
              │   PILLAR    │                    │   PILLAR    │
              │  (VPS/RPi)  │                    │  (VPS/RPi)  │
              └──────┬──────┘                    └──────┬──────┘
                     │ TCP                              │ TCP
       ┌─────────────┼─────────────┐                    │
       │             │             │                    │
  ┌────▼────┐   ┌────▼────┐   ┌────▼────┐          ┌────▼────┐
  │ Android │   │  ESP32  │◄──┤  Linux  │          │ Android │
  │  phone  │BLE│ SX1262  │   │ desktop │          │  phone  │
  └─────────┘   └────┬────┘   └─────────┘          └─────────┘
                     │ LoRa
              ┌──────▼──────┐
              │  nRF52840   │
              │   SX1262    │
              └─────────────┘
```

Every box is a full NEXUS node with its own identity. Roles only control *what extra work the node volunteers to do* (forwarding, store-and-forward, public TCP).

## Quick Start

### Run a pillar (Docker)

```sh
docker run -d --name nexus-pillar --restart unless-stopped \
  -p 4242:4242 -v nexus-pillar-data:/var/lib/nexus \
  idan2025/nexus-pillar:latest
docker logs -f nexus-pillar
```

The mounted volume persists the pillar's identity at `/var/lib/nexus/pillar.identity`, so the short address stays stable across restarts.

### Run a pillar (Linux, no Docker)

```sh
curl -fsSL https://raw.githubusercontent.com/idan2025/nexus-protocol/main/scripts/install-pillar.sh | sudo bash
```

Drops `pillard` in `/usr/local/bin`, creates a dedicated `nexus` system user, and enables a hardened systemd unit listening on TCP/4242. Re-running upgrades in place. Environment overrides: `PILLAR_PORT`, `PILLAR_TAG`, `PILLAR_NO_SERVICE`, `PILLAR_REPO`.

### Build the C library + tests

```sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Linux daemon

```sh
build/app/nexusd -l 4242 -p pillar.example.com:4242
```

Standalone C daemon with UDP multicast LAN discovery + TCP internet transport, identity persisted in `~/.nexus/identity`.

### Python bindings

```sh
cd bindings/python
PYTHONPATH=. python3 -m unittest discover tests/ -v
python3 cli.py identity generate
```

### Firmware (PlatformIO)

```sh
cd firmware/heltec_v3   && pio run -t upload     # ESP32-S3 + SX1262
cd firmware/rak4631     && pio run -t upload     # nRF52840 + SX1262
cd firmware/xiao_esp32s3 && pio run -t upload    # XIAO ESP32-S3 + WIO-SX1262
cd firmware/xiao_nrf52840 && pio run -t upload   # XIAO nRF52840 + WIO-SX1262
```

Prefer not to install PlatformIO? Flash any supported board over USB or BLE-DFU from your browser at the project's web-flasher.

### Android app

```sh
cd android && ./gradlew assembleRelease -PversionName=0.6.24 -PversionCode=37
```

Signed APKs are also attached to each GitHub release.

## Node Roles

| # | Role | Forwards? | Store-and-forward | Public TCP listen |
|---|------|-----------|-------------------|-------------------|
| 0 | LEAF | no | — | no |
| 1 | RELAY | yes | 8 slots / 30 min | no |
| 2 | GATEWAY | yes (bridges transports) | 8 slots / 30 min | optional |
| 3 | ANCHOR | yes | 32 slots / 1 h | optional |
| 4 | SENTINEL | yes | all capabilities | optional |
| 5 | PILLAR | yes | 32 slots / 1 h | **yes** |
| 6 | VAULT | yes | 32 slots / 24 h | no |

LEAF, RELAY, and GATEWAY are typical for end-user devices. ANCHOR, SENTINEL, PILLAR, VAULT are infrastructure and are hidden from the Android contacts/DM UI by default (still visible in the mesh and announce stream).

## Cryptography

| Primitive | Algorithm | Purpose |
|-----------|-----------|---------|
| AEAD | XChaCha20-Poly1305 | Packet encryption |
| Signatures | Ed25519 | Identity announcements |
| Key exchange | X25519 | Ephemeral + session DH |
| Hashing | BLAKE2b | Address derivation, KDF |
| Sessions | Double Ratchet | Forward secrecy |
| Groups | Sender-key chain | Efficient group crypto |

All primitives provided by [Monocypher](https://monocypher.org/) 4.0.2 (vendored, zero external dependencies).

## Packet Format

13-byte compact header, big-endian on wire:

```
[version(4b)][flags(4b)][ptype(4b)][rtype(4b)][hop(4b)][ttl(4b)]
[src_short(4B)][dst_short(4B)][seq(1B)][payload_len(1B)]
```

- Addresses: 16-byte full (BLAKE2b-128 of Ed25519 pubkey), 4-byte short on wire
- Ephemeral payload: `[eph_pub(32)][nonce(24)][MAC(16)][ciphertext]` — 72 B overhead
- Session payload: `[msg_num(4)][prev_n(4)][DH_pub(32)][nonce(24)][MAC(16)][ct]` — 80 B overhead
- Group payload: `[group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ct]` — 48 B overhead

Full wire details in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Project Structure

```
nexus/
├── lib/                       # Core C library (libnexus)
│   ├── include/nexus/         # Public headers
│   ├── src/                   # Implementation
│   ├── test/                  # 24 C test suites
│   └── vendor/monocypher/     # Monocypher 4.0.2
├── transports/
│   ├── serial/                # UART (0x7E framing)
│   ├── tcp/                   # Multi-peer Reticulum-style TCP
│   ├── udp/                   # UDP multicast LAN discovery
│   ├── lora/                  # LoRa + adaptive SF + duty cycle
│   ├── ble/                   # BLE (NUS GATT)
│   ├── wifi/                  # WiFi HaLow (802.11ah)
│   └── pipe/                  # In-memory (testing)
├── bindings/python/           # ctypes bindings, CLI, message builder
├── app/
│   ├── nexusd.c               # Linux daemon (multi-transport)
│   └── pillard.c              # Public pillar daemon
├── firmware/                  # PlatformIO firmware
│   ├── heltec_v3/             # Heltec WiFi LoRa 32 V3
│   ├── rak4631/               # RAK4631 WisBlock Core
│   ├── xiao_esp32s3/          # XIAO ESP32-S3 + WIO-SX1262
│   └── xiao_nrf52840/         # XIAO nRF52840 + WIO-SX1262
├── android/                   # Android app (Kotlin + Compose + NDK)
├── scripts/                   # install-pillar.sh, systemd unit
├── docs/                      # PROTOCOL, FEATURES, DEPLOYMENT, web-flasher
└── Dockerfile                 # Two-stage pillar image (idan2025/nexus-pillar)
```

## Supported Hardware

| Device | MCU / SoC | Transports |
|--------|-----------|------------|
| Heltec WiFi LoRa 32 V3 | ESP32-S3 + SX1262 | LoRa + BLE |
| RAK4631 WisBlock | nRF52840 + SX1262 | LoRa + BLE |
| XIAO ESP32-S3 (+ WIO-SX1262) | ESP32-S3 + SX1262 | LoRa + BLE |
| XIAO nRF52840 (+ WIO-SX1262) | nRF52840 + SX1262 | LoRa + BLE |
| Raspberry Pi / Linux SBC | — | TCP + UDP + Serial + BLE |
| Android phone | — | BLE (to ESP32/nRF), TCP (to pillar) |
| Any Linux VPS | — | TCP (as pillar) |

## Distribution

- **Docker image** — `docker.io/idan2025/nexus-pillar:latest` and `:v0.6.24`
- **Linux tarball** — `nexus-linux-x64.tar.gz` attached to each GitHub release (contains `pillard`, `nexusd`, `libnexus.a`, headers, systemd unit, installer)
- **Android APK** — signed APK attached to each GitHub release
- **Web flasher** — flash any supported board from your browser, no IDE required

## Documentation

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — wire format, crypto, routing
- [`docs/FEATURES.md`](docs/FEATURES.md) — feature matrix and capability notes
- [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md) — running pillars, gateways, firmware
- [`docs/FIRMWARE_DEBUG.md`](docs/FIRMWARE_DEBUG.md) — boot LED codes, JTAG, common faults
- [`scripts/README-pillar.md`](scripts/README-pillar.md) — pillar installer details

## License

MIT
