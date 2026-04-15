# NEXUS — Features To Implement

Comprehensive backlog to reach (and surpass) Reticulum/LXMF/Sideband parity.
Grouped by subsystem. Priority: **P0** blocker, **P1** parity, **P2** polish.

---

## 1. Android Client

### Messaging / UX
- **P0 ✅** File & image attachments end-to-end (picker → chunked `nx_node_send_large` → fragment reassembly → inline render).
- **P0 ✅** Voice-note recorder (AMR_NB 4750 bps, attach as `VOICE_NOTE` NXM field).
- **P0 ✅** Delivery receipts UI — sent/relayed/delivered/read states per bubble (hooks `ACK` + `READ` NXM types).
- **P1 ✅** Message search across conversations (Room FTS4 index on plaintext).
- **P1 ✅** Announce Stream screen — live feed of announces received, tap to add contact.
- **P1 ✅** Contacts / Address Book with nicknames, sync via `CONTACT` NXM type.
- **P1 ✅** Paper-messaging QR export/import (encrypted single-message blobs, LXMF parity).
- **P2** Reactions picker UI (emoji + `REACTION` NXM, already on wire).
- **P2** Typing indicator UI + `TYPING` field relay.
- **P2** Dark theme + Material You dynamic color.

### Identity & Storage
- **P0 ✅** Identity backup/export (encrypted file, passphrase-wrapped).
- **P0 ✅** Identity restore on new device + migration of conversations.
- **P1** Secondary identities (multi-account switcher).
- **P1** Per-conversation encryption at rest (passphrase unlock gate).

### Transport / Connectivity
- **P0 ✅** Pillar **inbox pull** — auto-requests on discovery of anchor/pillar/vault neighbor (once per session).
- **P0 ✅** Battery-optimization whitelist prompt on first run.
- **P1 ✅** Wi-Fi-only / metered-aware Pillar connect policy.
- **P1** Multi-Pillar failover (try next on timeout, currently connects to all in parallel).
- **P2** In-app Pillar discovery (scan a well-known list / DNS-SD).
- **P2** Tor / Orbot SOCKS proxy for Pillar connection.

### Telemetry & Ops
- **P1 ✅** Node telemetry screen — RSSI/SNR per neighbor, duty-cycle, reassembly stats, session count.
- **P1 ✅** Route inspector (tap a contact → RREQ graph, hops, via_transport).
- **P2** Background announce cadence setting (default 5 min).

### Security (LXMF parity)
- **P1 ✅** Proof-of-Work stamps on outbound messages (configurable difficulty, anti-spam).
- **P1** Stamp verification UI (reject / warn on under-difficulty inbound).
- **P2** Per-contact trust levels (verified / seen / unknown).

---

## 2. Pillar Server (`pillard`)

- **P0 ✅** Pillar **inbox fetch protocol** — client sends `NX_EXTHDR_INBOX_REQ`, pillar replays stored packets addressed to sender.
- **P0 ✅** Per-peer rate limiting (token bucket, WARN on overflow).
- **P1 ✅** Optional peer auth — X25519 pre-shared cert or allow-list of short addrs.
- **P1 ✅** HTTP stats endpoint (`/metrics`, Prometheus text format): connected_peers, mailbox_depth, bytes/sec, dedup_hits.
- **P1** Multi-pillar mailbox sync — on federation, gossip message IDs + pull missing.
- **P1 ✅** systemd `ExecReload` wired to `SIGHUP` re-announce (done) + `/reload` admin UDS.
- **P2** GeoIP / ASN aware peer selection hints.
- **P2** TLS wrap option (pillard listens on 4242 + 4243-TLS for hostile networks).
- **P2** Web admin (read-only, journal-style dashboard).

---

## 3. Firmware (ESP32-S3 / nRF52840 / XIAO / Heltec / RAK)

### Correctness (see FIRMWARE_DEBUG.md)
- **P0 ✅** Non-blocking LoRa RX in `radiolib_hal.cpp` (DIO1 ISR + `yield()` poll, no blocking `receive()`).
- **P0 ✅** Resolve `setDio2AsRfSwitch` vs external RXEN conflict — gated on `NX_LORA_RF_SWITCH_EXTERNAL` per board.
- **P0 ✅** Region-aware default LoRa config (EU868/US915/AU915/AS923/KR920/IN865 build flags).

### Features
- **P1 ✅** OLED status pages (Heltec V3 / T-Beam): address, RSSI last, battery, tx/rx counters.
- **P1** Battery voltage reporting on announces (telemetry exthdr).
- **P1 ✅** Button UX: short = toggle screen, long = broadcast distress announce.
- **P1** Persistent ring-buffer of last 32 inbound messages (NVS/InternalFS) for BLE resync after reboot.
- **P2** GPS integration (NEO-6M / built-in L76K on T-Beam) → auto `LOCATION` field.
- **P2** Solar-power deep-sleep schedule with wake-on-LoRa-preamble.
- **P2** OTA firmware update via BLE.

### Settings / Config
- **P1** Settings round-trip (ESP32 NVS already present) mirrored on nRF52 InternalFS — verify end-to-end from Android.
- **P2** Role-change requires reboot — do it live (re-apply anchor tier).

---

## 4. Protocol Core (libnexus)

- **P0 ✅** Inbox pull request exthdr (`NX_EXTHDR_INBOX_REQ=0x30`) + `nx_node_request_inbox()` + mailbox replay.
- **P1 ✅** LXMF parity fields in NXM: title, content-type mime, source-nickname.
- **P1 ✅** Paper-message envelope (offline single-hop, 2kB cap, QR friendly).
- **P1 ✅** PoW stamp field on envelope (LXMF-style, difficulty in announce).
- **P1 ✅** Link/session resume across reboots (persisted ratchet state).
- **P2** Multi-hop path selection metric (RSSI-weighted, not just hop count).
- **P2** Opportunistic forwarding (store pkt for N seconds even without mailbox slot).
- **P2** Rate-limited broadcast storm suppression (announce jitter + backoff).

---

## 5. CLI / Tooling

- **P1 ✅** `nexus-cli send <addr> <text>` one-shot send over local nexusd socket.
- **P1 ✅** `nexus-cli inbox` list mailbox contents.
- **P2** `nexus-cli pillar-probe host:port` — latency / version handshake.
- **P2** Man pages for `nexusd(8)`, `pillard(8)`, `nexus-cli(1)`.

---

## 6. Testing / CI

- **P1** Integration test: Android emulator ↔ nexusd ↔ pillard ↔ second emulator.
- **P1 ✅** Fuzzer on packet parser (libFuzzer + Monocypher stub).
- **P2** Soak test rig: 32-node Docker swarm, 1M-packet torture.
- **P2** Interop harness against Reticulum `rnsd` (shared TCP only — different crypto, won't decode, but proves framing coexistence).

---

## 7. Documentation

- **P1 ✅** `docs/PROTOCOL.md` — on-wire format reference (header, exthdr types, NXM TLV).
- **P1 ✅** `docs/DEPLOYMENT.md` — Pillar sizing, firewalling, DNS, federation patterns.
- **P2** `docs/ANDROID_DEV.md` — JNI contract, adding new NXM fields.
- **P2** Architecture diagram (mermaid) for README.
