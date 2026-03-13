# NEXUS Implementation Plan

**Network EXchange Unified System** -- E2E encrypted, multi-transport mesh protocol.

## Status Overview

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Foundation (MVP) | **DONE** |
| 1 | Routing Core (PRISM) | **DONE** |
| 2 | LoRa + MCU Port | **DONE** |
| 3 | Fragmentation + Store-and-Forward | **DONE** |
| 4 | Linked Sessions + Forward Secrecy | **DONE** |
| 5 | Groups + Domains + Gateway | **DONE** |
| 6 | Python Bindings + CLI | **DONE** |
| 7 | Advanced Features | **DONE** |
| 8 | Android App + Production Firmware | **DONE** |
| 9 | Messenger Feature Parity | **DONE** |

**Total**: 75+ source files, ~15500+ lines of code, 20/20 C test suites + 43/43 Python tests passing

---

## Phase 0: Foundation (MVP) -- COMPLETE

**Goal**: Core library skeleton with identity, packet format, crypto, serial transport, and unit tests.

### What was built

**Headers** (`lib/include/nexus/`):
- `types.h` -- Core types, constants, error codes, packet/address/identity structs
- `platform.h` -- Platform abstraction interface (random, time, memory, mutex)
- `identity.h` -- Key generation and address derivation API
- `packet.h` -- 13-byte compact header serialize/deserialize API
- `crypto.h` -- AEAD encrypt/decrypt, X25519 key exchange, ephemeral mode API
- `transport.h` -- Transport vtable interface, registry, serial config

**Implementation** (`lib/src/`):
- `platform/platform_posix.c` -- Linux/POSIX: /dev/urandom, clock_gettime, pthreads
- `identity.c` -- Ed25519 keypair gen, X25519 derivation, BLAKE2b-128 address hashing
- `packet.c` -- Big-endian wire format serialize/deserialize, flag encoding
- `crypto.c` -- XChaCha20-Poly1305 AEAD, X25519+BLAKE2b key derivation, ephemeral mode
- `transport.c` -- Transport registry (up to 8 transports)

**Serial Transport** (`transports/serial/`):
- `nexus_serial.c` -- UART transport with `[0x7E][LEN_HI][LEN_LO][PAYLOAD][0x7E]` framing, poll-based receive with timeout

**Vendored Deps** (`lib/vendor/`):
- `monocypher/` -- Monocypher 4.0.2 (monocypher.c + monocypher.h)

**Tests** (`lib/test/`):
- `test_identity.c` -- 6 tests: generation, uniqueness, address derivation, determinism, wipe, null safety
- `test_packet.c` -- 8 tests: flag encoding, hop/ttl, serialize/deserialize roundtrip, empty payload, wire size, buffer bounds, all flag combos
- `test_crypto.c` -- 7 tests: AEAD roundtrip, tamper detection, AD binding, X25519 key agreement, ephemeral encrypt/decrypt, wrong-key rejection, tamper detection

### Stats (Phase 0)
- 20 source files, ~1700 lines of code
- 21/21 tests passing

### Key Design Decisions
- **Packet header**: 13 bytes, big-endian, short addresses on wire (4 bytes)
- **Ephemeral payload layout**: `[eph_pubkey(32)][nonce(24)][MAC(16)][ciphertext]` = 72 bytes overhead
- **Serial framing**: `0x7E` delimiters + 2-byte big-endian length prefix, no byte stuffing
- **Address derivation**: `BLAKE2b-128(Ed25519_pubkey)` -> 16-byte full addr, first 4 bytes -> short addr
- **X25519 key derivation**: `BLAKE2b-256(raw_X25519_shared_secret)` -> 32-byte symmetric key

---

## Phase 1: Routing Core (PRISM) -- COMPLETE

**Goal**: Hybrid routing engine, signed identity announcements, node lifecycle, TCP transport.

### What was built

**Headers** (`lib/include/nexus/`):
- `announce.h` -- Signed identity announcement create/parse/verify API
- `route.h` -- PRISM routing: neighbor table, route cache, dedup, RREQ/RREP/RERR/BEACON
- `node.h` -- Node lifecycle, event loop, send API, callbacks
- `transport.h` -- Extended with `nx_tcp_config_t` and `nx_tcp_transport_create()`

**Implementation** (`lib/src/`):
- `announce.c` -- Ed25519-signed announcements (130-byte payload: sign_pub + x25519_pub + role + flags + signature). Create, parse with signature verification, build announce packets.
- `route.c` -- Full PRISM routing engine:
  - Neighbor table (32 entries): add/update/find/expire, stores pubkeys + role + RSSI + link quality
  - Route table (64 entries): add/update with metric preference, expire, invalidate-via
  - Dedup table (128 entries): (src, seq_id) tracking with expiry, LRU eviction
  - RREQ/RREP/RERR/BEACON payload builders and parsers
  - `nx_route_process()` -- handles incoming route packets: RREQ installs reverse route, RREP installs forward route, RERR invalidates routes
- `node.c` -- Complete node lifecycle:
  - Init with random or existing identity
  - Event loop (`nx_node_poll`): polls all transports, dispatches packets, sends periodic beacons, expires stale entries
  - Packet dispatch: announcements update neighbor table + install direct routes; route packets processed by PRISM; data packets delivered via callback or forwarded
  - `nx_node_send()` -- encrypted data via ephemeral mode (looks up recipient X25519 pubkey from neighbor table)
  - `nx_node_send_raw()` -- unencrypted data for testing
  - `nx_node_announce()` -- broadcast signed announcement on all transports
  - Flood forwarding with TTL decrement, role-based relay enforcement (LEAF never forwards)

**TCP Transport** (`transports/tcp/`):
- `nexus_tcp.c` -- TCP point-to-point transport with same `0x7E` framing as serial. Server mode (listen + lazy accept) and client mode (connect). TCP_NODELAY for low latency. For mesh simulation, each node creates multiple TCP transports (one per link).

**Tests** (`lib/test/`):
- `test_announce.c` -- 6 tests: create/parse roundtrip, tamper detection, wrong-signer rejection, build_packet, role preservation, buffer bounds
- `test_route.c` -- 16 tests: neighbor CRUD + expire + full table, route CRUD + metric preference + expire + invalidate-via, dedup basic + expire, RREQ/RREP/RERR/BEACON builders, process RREQ/RREP/RERR

### Stats (Phase 1)
- 9 new source files, ~2100 new lines
- 22 new tests (6 announce + 16 route)

### Key Design Decisions
- **Announcement payload**: `[sign_pub(32)][x25519_pub(32)][role(1)][flags(1)][Ed25519_sig(64)]` = 130 bytes. Signature covers first 66 bytes.
- **Routing sub-types**: RREQ(12B), RREP(13B), RERR(5B), BEACON(3B) -- all carried in PTYPE_ROUTE packets
- **Route preference**: Lower metric wins; equal metric updates the route (freshness)
- **Dedup**: 30-second window, 128-entry table with LRU eviction
- **Node roles enforced**: LEAF nodes never forward; RELAY+ nodes forward flooded and routed packets
- **Beacons**: 15-second interval, broadcast announce on all transports
- **TCP transport**: Lazy accept (server doesn't block on init), SO_REUSEADDR, TCP_NODELAY
- **Static table sizes** (MCU-safe): 32 neighbors, 64 routes, 128 dedup, 16 pending RREQ

---

## Phase 2: LoRa + MCU Port -- COMPLETE

**Goal**: LoRa radio transport with hardware abstraction, MCU platform layers, PlatformIO firmware projects, desktop-testable via mock radio.

### What was built

**Headers** (`lib/include/nexus/`):
- `lora_radio.h` -- LoRa radio HAL abstraction: vtable with init/transmit/receive/cad/sleep/standby/reconfigure/destroy. Config struct for frequency/BW/SF/CR/power/preamble/sync_word/CRC. Airtime calculation function. Mock radio API for desktop testing.

**Implementation**:
- `transports/lora/nexus_lora.c` -- LoRa transport:
  - CAD-based collision avoidance with random backoff (20-200ms, up to 5 retries)
  - 1% duty cycle enforcement (1-minute sliding window)
  - Airtime calculation using the LoRa modem formula (accounts for SF, BW, CR, preamble, CRC, header mode, low data rate optimization)
  - No framing delimiters (radio handles packet boundaries via preamble + sync word)
  - Mock radio: ring buffer (16 packets), bidirectional peer linking, simulated RSSI/SNR

**MCU Platform Layers** (`lib/src/platform/`):
- `platform_esp32.c` -- FreeRTOS: `esp_fill_random()` for HW RNG, `xTaskGetTickCount()` for time, `pvPortMalloc/vPortFree` for memory, FreeRTOS mutex. Guarded by `#ifdef NX_PLATFORM_ESP32`.
- `platform_nrf52.c` -- Zephyr RTOS: `sys_rand_get()` for HW TRNG, `k_uptime_get()` for time, 16KB static heap (`K_HEAP_DEFINE`) to avoid fragmentation, static mutex pool (4 max). Guarded by `#ifdef NX_PLATFORM_NRF52`.

**Firmware** (`firmware/`):
- `heltec_v3/` -- PlatformIO project for Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262). Pin mapping: SS=8, DIO1=14, RST=12, BUSY=13. Arduino framework + RadioLib.
- `rak4631/` -- PlatformIO project for RAK4631 WisBlock Core (nRF52840 + SX1262). Standard WisBlock pin mapping. Arduino framework + RadioLib.
- Both firmware projects: init RadioLib SX1262, create NEXUS RELAY node, announce, poll loop.

**Tests** (`lib/test/`):
- `test_lora.c` -- 10 tests: mock create/destroy, linked loopback (bidirectional), receive timeout, unlinked transmit fail, CAD, sleep/standby states, LoRa transport send/recv via mock, airtime calculation (value ranges, SF comparison, empty payload), null config, multiple queued packets (FIFO order).

### Stats (Phase 2)
- 8 new source files, ~1300 new lines
- 10 new tests (53 total)

### Key Design Decisions
- **Radio HAL abstraction**: Pure C vtable (`nx_lora_radio_ops_t`) decouples transport logic from hardware. Firmware wraps RadioLib (C++) behind this C interface.
- **Mock radio**: In-memory ring buffer with peer linking. Enables full transport testing on desktop without hardware.
- **No LoRa framing**: Unlike serial/TCP, LoRa packets don't need `0x7E` delimiters -- the radio's preamble + sync word + length field handles packet boundaries.
- **CAD collision avoidance**: Check channel before transmit, random backoff on busy, up to 5 retries.
- **1% duty cycle**: 60-second sliding window, max 600ms TX per minute. Returns `NX_ERR_FULL` if budget exceeded.
- **nRF52 memory**: 16KB static heap via `K_HEAP_DEFINE` (avoids malloc fragmentation), static mutex pool (4 slots).
- **Airtime formula**: Full LoRa modem calculation accounting for SF, BW, CR, preamble, CRC, implicit/explicit header, low data rate optimization.
- **MCU layers conditionally compiled**: `#ifdef NX_PLATFORM_ESP32` / `NX_PLATFORM_NRF52` -- not compiled on desktop, only on target toolchains.

---

## Phase 3: Fragmentation + Store-and-Forward -- COMPLETE

**Goal**: Fragment large messages across constrained links, store messages for offline nodes on ANCHOR nodes.

### What was built

**Headers** (`lib/include/nexus/`):
- `fragment.h` -- Fragmentation & reassembly API. Extended header format: `[type(1)][frag_id(2)][idx_total(1)]` = 4 bytes. Type byte (0x01 = fragment) enables typed exthdr dispatch. idx_total packs fragment index (upper nibble, 0-15) and total count (lower nibble, 1-16). Up to 16 fragments * 238 bytes = 3808 byte max message.
- `anchor.h` -- ANCHOR store-and-forward mailbox. 32 slots, 8 per-destination limit, configurable TTL (1 hour default).

**Implementation** (`lib/src/`):
- `fragment.c` -- Full fragmentation engine:
  - `nx_frag_split()`: Splits messages into packets with FRAG and EXTHDR flags. Small messages (<= NX_MAX_PAYLOAD) pass through unfragmented. Each fragment carries 3-byte extended header + up to 239 bytes of data.
  - `nx_frag_receive()`: Reassembly buffer with 8 concurrent slots. Collects fragments keyed by (src, frag_id). Uses bitmask for tracking received indices. Handles out-of-order delivery and duplicate fragments. Returns complete message when all fragments received.
  - `nx_frag_expire()`: Cleans up incomplete reassemblies after 30 seconds.
  - Evicts oldest slot when buffer is full.
- `anchor.c` -- ANCHOR mailbox:
  - `nx_anchor_store()`: Stores complete packets with per-destination limit (8 max).
  - `nx_anchor_retrieve()`: Returns and removes all messages for a destination (with max limit). Used when destination comes online.
  - `nx_anchor_expire()`: Removes messages past their TTL.

**Node Integration** (`lib/src/node.c` + `lib/include/nexus/node.h`):
- `nx_node_t` now includes `nx_frag_buffer_t` and `nx_anchor_t` fields.
- `handle_data()` detects EXTHDR flag and routes to fragment reassembly. Complete messages delivered to application callback.
- `handle_data()` forwarding: ANCHOR nodes store unroutable packets for offline destinations.
- `handle_announce()`: ANCHOR nodes deliver stored messages when destination comes back online.
- `nx_node_send_large()`: Auto-fragments messages larger than NX_MAX_PAYLOAD.
- `nx_node_poll()`: Expires fragment buffer and anchor mailbox alongside routing table.

**Tests** (`lib/test/`):
- `test_fragment.c` -- 8 tests: exthdr encode/decode roundtrip, small passthrough, 1000-byte fragment+reassemble, out-of-order delivery, duplicate fragment handling, incomplete timeout expiry, max message (3808B, 16 fragments), oversized rejection.
- `test_anchor.c` -- 6 tests: store/retrieve, per-dest limit, TTL expiry, full mailbox, empty retrieve, retrieve with max_pkts limit.

### Stats (Phase 3)
- 6 new source files, ~1100 new lines
- 14 new tests (67 total)

### Key Design Decisions
- **Fragment extended header**: 4 bytes `[type(1)][frag_id(2)][idx_total(1)]` prepended to payload. Type byte (0x01) enables typed exthdr dispatch for sessions (0x10-0x12) vs fragments. NX_FLAG_FRAG set on all fragments except the last.
- **Fragment capacity**: 238 bytes per fragment (NX_MAX_PAYLOAD - 4). 16 fragments max = 3808 bytes max message.
- **Reassembly buffer**: 8 concurrent reassemblies, bitmask tracking (uint16_t supports 16 fragments). Handles out-of-order and duplicates. 30-second timeout with LRU eviction.
- **Anchor mailbox**: 32 slots total, 8 per destination. 1-hour default TTL. Packets stored as complete `nx_packet_t` for direct retransmission.
- **Anchor delivery**: Triggered by announcement receipt -- when a previously-offline node announces, ANCHOR retrieves and transmits all stored messages.
- **Memory budget**: Reassembly buffer ~31KB (8 slots * 3.8KB), anchor ~8.5KB (32 * 271B). Combined ~40KB fits nRF52840's 256KB SRAM budget.

---

## Phase 4: Linked Sessions + Forward Secrecy -- COMPLETE

**Goal**: Per-message forward secrecy via Signal-style Double Ratchet, integrated into the node layer with typed extended header dispatch.

### What was built

**Headers** (`lib/include/nexus/`):
- `session.h` -- Double Ratchet session API. Session store (16 slots), handshake (simplified X3DH: identity-identity + ephemeral-identity DH), encrypt/decrypt with chain ratchet and DH ratchet, skipped message key storage (32 slots) for out-of-order messages.
- `types.h` -- Added `NX_EXTHDR_FRAGMENT` (0x01) constant for typed exthdr dispatch.
- `fragment.h` -- Updated exthdr size 3→4 bytes (added type byte prefix).
- `node.h` -- Added `nx_session_store_t` to node state, `nx_on_session_fn` callback, `nx_node_session_start()` and `nx_node_send_session()` API, `NX_SESSION_MAX_PLAINTEXT` (161 bytes) constant.

**Implementation** (`lib/src/`):
- `session.c` -- Full Double Ratchet engine:
  - KDF helpers: `kdf_rk()` (root key ratchet via BLAKE2b-keyed) and `kdf_ck()` (chain key ratchet for message keys)
  - Session store: init/find/alloc/remove/count with `crypto_wipe()` on removal
  - Handshake: `nx_session_initiate()` (Alice generates ephemeral, computes DH1+DH2, derives root key), `nx_session_accept()` (Bob derives same root key, generates own ephemeral, performs first DH ratchet), `nx_session_complete()` (Alice receives ACK, performs DH ratchet, generates new ephemeral for sending)
  - Encrypt: derives message key via chain KDF, writes `[msg_num(4)][prev_n(4)][DH_pub(32)][nonce(24)][MAC(16)][ciphertext]` (80 bytes overhead)
  - Decrypt: checks skipped keys, handles DH ratchet advancement on new remote key, derives message key, verifies MAC
  - Skipped keys: stores up to 32 skipped message keys for out-of-order delivery
- `fragment.c` -- Updated encode/decode to write/verify type byte (0x01) at offset 0.
- `node.c` -- Session integration:
  - Typed exthdr dispatch in `handle_data()`: switch on first payload byte (0x01=fragment, 0x10=SESSION_INIT, 0x11=SESSION_ACK, 0x12=SESSION_MSG)
  - `handle_session_init()`: looks up sender's X25519 pubkey, allocates session, calls `nx_session_accept()`, sends ACK packet
  - `handle_session_ack()`: finds session, calls `nx_session_complete()`
  - `handle_session_msg()`: finds established session, decrypts, delivers via `on_session` callback
  - `nx_node_session_start()`: looks up peer pubkey, allocates session, calls `nx_session_initiate()`, sends INIT packet
  - `nx_node_send_session()`: finds established session, encrypts with ratchet, sends MSG packet
  - Init/stop: session store init on node init, `crypto_wipe()` on all sessions at stop

**Tests** (`lib/test/`):
- `test_session.c` -- 10 tests: store alloc/find, store remove/wipe, handshake establishment, encrypt/decrypt roundtrip, bidirectional messages, forward secrecy verification, multiple same-direction messages, tamper detection, unique ciphertexts, unestablished session rejection
- `test_session_node.c` -- 5 tests: session establishment through node layer (INIT→ACK→complete), send/receive session message with callback, bidirectional session messages, session_start fails without neighbor, send_session fails without established session

### Stats (Phase 4)
- 3 new source files (~1370 new lines), 6 modified files
- 15 new tests (82 total)

### Key Design Decisions
- **Typed exthdr system**: 1-byte type prefix on all extended headers. Fragment type = 0x01, session types = 0x10-0x12. Enables future exthdr types without flag exhaustion.
- **Simplified X3DH handshake**: 2 DH computations (identity×identity + ephemeral×identity) instead of Signal's 4. Sufficient for peer-to-peer without prekeys.
- **Wire format**: Session payload `[msg_num(4)][prev_n(4)][DH_pub(32)][nonce(24)][MAC(16)][ciphertext]` = 80 bytes overhead. Max plaintext per session message = 161 bytes.
- **DH ratchet**: Advances on sender change (Alice→Bob, Bob→Alice). Provides forward secrecy per direction change.
- **Chain ratchet**: Derives unique message key per message via BLAKE2b-keyed KDF. Chain key replaced after each derivation.
- **Skipped keys**: Up to 32 stored message keys for out-of-order delivery. Keyed by (DH_pub, msg_num).
- **Session packet sub-types**: INIT (0x10) carries ephemeral pubkey (32B), ACK (0x11) carries responder ephemeral (32B), MSG (0x12) carries ratcheted ciphertext.
- **Memory budget**: Session store ~21KB (16 sessions × ~1.3KB each). Total with fragment + anchor: ~72KB.

---

## Phase 5: Groups + Domains + Gateway -- COMPLETE

**Goal**: BLE transport, group encryption with sender-key chain ratchet, GATEWAY cross-transport bridging, integrated into node layer.

### What was built

**Headers** (`lib/include/nexus/`):
- `ble_radio.h` -- BLE radio HAL abstraction: vtable with init/send/recv/sleep/destroy. Config struct for peripheral/central mode and MTU. Mock radio API for desktop testing.
- `group.h` -- Group encryption API. Group store (8 groups, 16 members each), sender-key chain ratchet, encrypt/decrypt. Wire format: `[group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]` = 48 bytes overhead.
- `transport.h` -- Extended with `domain_id` field on `nx_transport_t` and `nx_ble_transport_create()`.
- `route.h` -- Extended `nx_route_t` with `via_transport` field for cross-transport route tracking.
- `node.h` -- Added `nx_group_store_t` to node state, `nx_on_group_fn` callback, `nx_node_group_create/add_member/send()` API.

**Implementation**:
- `transports/ble/nexus_ble.c` -- BLE transport:
  - Direct radio send/recv (no framing -- BLE GATT PDUs are self-delimiting)
  - No duty cycle or CAD (BLE stack handles these)
  - Mock radio: ring buffer (16 packets), bidirectional peer linking
- `lib/src/group.c` -- Group encryption engine:
  - Sender-key derivation: `BLAKE2b_keyed(group_key, member_addr)` per member
  - Chain ratchet: `kdf_ck()` (same pattern as session.c) derives message key + next chain key
  - Encrypt: advances send chain, writes `[group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]` with AD = first 8 bytes
  - Decrypt: finds sender in members table, advances their chain to match msg_num (skip-ahead), derives message key, verifies MAC
  - Group store: create/find/remove with `crypto_wipe()` on removal
- `lib/src/node.c` -- Gateway bridging + group integration:
  - `transmit_bridge()`: sends on all active transports except ingress (prevents echo)
  - GATEWAY nodes use `transmit_bridge()` instead of `transmit_all()` for forwarding and flood relay
  - RELAY nodes continue to use `transmit_all()` (echo back included)
  - NX_RTYPE_DOMAIN handled alongside NX_RTYPE_ROUTED for cross-domain forwarding
  - Typed exthdr dispatch: NX_EXTHDR_GROUP_MSG (0x20) → `handle_group_msg()`
  - `handle_group_msg()`: extracts group_id, finds group, decrypts, delivers via `on_group` callback
  - `nx_node_group_send()`: encrypts with group sender key, broadcasts as FLOOD with exthdr

**Tests** (`lib/test/`):
- `test_ble.c` -- 6 tests: mock create/destroy lifecycle, linked bidirectional loopback, receive timeout, unlinked send fail, full transport send/recv via mock (serialize+deserialize), multiple queued packets (FIFO order)
- `test_group.c` -- 8 tests: store create/find, store remove/wipe, add member + duplicate rejection, encrypt/decrypt roundtrip, multiple senders (both directions), chain ratchet (5 messages, unique ciphertexts, msg_num advances), tamper detection, wrong group key rejection
- `test_gateway.c` -- 7 tests: gateway cross-transport forward (Alice→GW→Bob), gateway no-echo (ingress excluded), RELAY sends on all (no bridging), group send/recv via node layer, group bidirectional messages, group_send fails for unknown group, non-member ignores group message

### Stats (Phase 5)
- 6 new source files, ~1700 new lines
- 21 new tests (103 total)

### Key Design Decisions
- **BLE HAL abstraction**: Same vtable pattern as LoRa (`nx_ble_radio_ops_t`). Simpler state struct (no duty cycle, no CAD). Mock radio identical in structure to LoRa mock.
- **No BLE framing**: BLE GATT PDUs are self-delimiting (unlike serial/TCP which need `0x7E` framing). Direct send/recv on radio.
- **Group sender keys**: Each member's chain key derived as `BLAKE2b_keyed(group_key, addr)`. Ensures each sender has a unique chain. Reuses `kdf_ck()` pattern from Double Ratchet for per-message key derivation.
- **Group wire format**: `[group_id(4)][msg_num(4)][nonce(24)][MAC(16)][ciphertext]` = 48 bytes overhead. Max plaintext = 193 bytes. AD = group_id + msg_num (8 bytes).
- **Group exthdr type**: 0x20 for GROUP_MSG, extends the typed exthdr system from Phase 4 (0x01=fragment, 0x10-0x12=session).
- **Gateway bridging**: `transmit_bridge(pkt, exclude_idx)` sends on all active transports except the one the packet arrived on. GATEWAY role (>= NX_ROLE_GATEWAY) enables bridging; RELAY role uses `transmit_all()`.
- **Domain routing**: NX_RTYPE_DOMAIN treated like NX_RTYPE_ROUTED for forwarding -- GATEWAY nodes bridge it cross-transport.
- **Transport domain_id**: `uint8_t domain_id` on `nx_transport_t` enables domain segmentation (0 = default). Routes carry `via_transport` for transport-aware forwarding.
- **Memory budget**: Group store ~5KB (8 groups * ~640B). Total system: ~77KB (sessions 21KB + fragment 31KB + anchor 8.5KB + groups 5KB + misc).

---

## Phase 6: Python Bindings + CLI -- COMPLETE

**Goal**: Python ctypes bindings to libnexus with CLI tool and integration tests.

### What was built

**Python Package** (`bindings/python/nexus/`):
- `_ffi.py` -- ctypes FFI layer: all struct definitions (CIdentity, NodeState, NodeConfig, Header, Packet, Transport, RouteTable, FragBuffer, Anchor, SessionStore, GroupStore), verified with sizeof/offsetof assertions. Library auto-discovery (env var, relative path, system). Function signatures for identity, transport, node lifecycle, session, group APIs. Neighbor/route injection bindings for testing.
- `errors.py` -- `NexusError` exception with error code mapping, `check()` helper.
- `identity.py` -- `Identity` class: generate, save/load to file, key/address access, wipe.
- `transport.py` -- Transport registry helpers: `registry_init()`, `create_pipe_pair()`, `set_active()`. Module-level reference tracking prevents ctypes GC of pipe pointers. Uses C `nx_transport_set_active()` to avoid ctypes proxy issues.
- `node.py` -- `Node` class: init with role/identity/callbacks, poll/drain, announce, send_raw/send/send_large, session_start/send_session, group_create/add_member/group_send, inject_neighbor (for testing), stop with cleanup.
- `__init__.py` -- Public API exports.

**CLI Tool** (`bindings/python/cli.py`):
- `nexus-cli identity generate` -- Generate and print a new identity (short_addr, sign_pub, x25519_pub).
- `nexus-cli identity save <file>` -- Generate and save identity to binary file.
- `nexus-cli identity show <file>` -- Load and display identity from file.

**C Additions**:
- `nx_transport_set_active()` -- C function for setting transport active flag (avoids ctypes proxy GC issues).

**Tests** (`bindings/python/tests/`):
- `test_identity.py` -- 4 tests: generate, unique, wipe, save/load roundtrip.
- `test_node.py` -- 5 tests: init/stop, poll empty, send_raw/recv, announce/neighbor callback, node identity access.
- `test_group.py` -- 4 tests: group send/recv, bidirectional, unknown group rejection, non-member ignores.
- `test_session.py` -- 4 tests: handshake establishment, send/recv with callback, bidirectional messages, no-neighbor rejection. Uses direct neighbor injection (matching C test pattern) for reliable transport toggling.

### Stats (Phase 6)
- 9 new Python files (~1200 new lines), 1 modified C file
- 17 new Python tests (103 C + 17 Python = 120 total)

### Key Design Decisions
- **ctypes FFI**: Direct binding to libnexus.so via ctypes. All struct layouts verified with sizeof assertions matching C `offsetof` values. No CFFI or Cython dependency.
- **Transport reference tracking**: Module-level `_transport_refs` list prevents Python GC from collecting ctypes Transport pointers while pipes are registered in the global C registry.
- **C set_active function**: `nx_transport_set_active()` avoids ctypes `.contents` proxy object lifetime issues when toggling transport active flags from Python.
- **Direct neighbor injection**: Session tests use `inject_neighbor()` (wrapping `nx_neighbor_update` + `nx_route_update`) instead of announce exchange, matching the reliable C test pattern and avoiding flaky interactions with shared transport registry.
- **Library auto-discovery**: Checks `NEXUS_LIB_PATH` env var, then `../../build/lib/libnexus.so` relative to package, then system library path.

---

## Phase 7: Advanced Features -- COMPLETE

**Goal**: WiFi HaLow transport, adaptive LoRa spreading factor, 1000+ node stress test, security audit.

### What was built

**WiFi HaLow Transport** (`transports/wifi/`):
- `nexus_halow.c` -- 802.11ah transport with raw socket (Linux) or stub mode
- `halow.h` -- Config struct (channel, bandwidth, rate, security, mesh, power save)
- Power save modes: ACTIVE, WMM, TIM, NON-TIM
- Security modes: OPEN, WPA3-SAE, WPA2-PSK, OWE
- Channel assessment, airtime estimation, reconfiguration, metrics/stats APIs
- Graceful fallback to stub mode without root (no raw socket required for testing)

**Adaptive Spreading Factor** (`transports/lora/`):
- `nexus_lora_asf.c` -- SF7-SF12 dynamic selection based on link quality
- `lora_asf.h` -- Opaque state, 4 strategies: conservative, balanced, aggressive, adaptive
- History-based tracking (16-slot ring buffer), running RSSI/SNR averages
- Duty cycle awareness, airtime estimation per SF, configurable bounds
- Reset, force, strategy switch APIs

**1024-Node Stress Test** (`lib/test/test_stress_1k.c`):
- 1024 identity generation + uniqueness verification (0 collisions in 4-byte address space)
- AEAD lock/unlock throughput: ~64K ops/sec
- X25519 key exchange: ~2700 exchanges/sec
- Route table saturation: 32 neighbors + 64 routes filled
- Packet serialize/deserialize: ~10M ops/sec
- ASF strategy sweep: 4 strategies x 256 iterations
- Node lifecycle: 128 nodes x 8 batches (1024 total init/stop cycles)
- Completes in <1 second

**Security Audit -- Fixes Applied**:
- Fixed integer underflow in `skip_message_keys()` (session.c) -- `until < recv_chain.n` guard
- Fixed dead code in DH ratchet path (session.c:409) -- was passing 0 unconditionally
- Replaced unsafe `inet_addr()` with `inet_pton()` + validation (nexus_tcp.c)
- Added bounds guard on int-to-uint8_t cast for RREP payload length (node.c)
- Improved LoRa backoff PRNG from 8-bit to 16-bit entropy (nexus_lora.c)
- HaLow transport: tolerates socket failure gracefully (stub mode)

**Tests** (`lib/test/`):
- `test_halow.c` -- 16 tests: create/destroy, init, send, recv, metrics, stats, reconfigure, airtime, assess, null safety
- `test_asf.c` -- 14 tests: create, clamp, null safety, strategy names, bounds, force, record, airtime, reset, all strategies, link quality
- `test_stress_1k.c` -- 8 tests: identity gen, uniqueness, AEAD, X25519, route table, packet ser/de, ASF sweep, node lifecycle

### Test counts
- New C test suites: 3 (test_halow, test_asf, test_stress_1k)
- New sub-tests: 38 (16 + 14 + 8)
- Total: 17 C test suites (all pass), 17 Python tests (all pass)

---

## Phase 8: Android App + Production Firmware

**Goal**: Ship a usable encrypted mesh messenger -- Android app as primary UI, ESP32/nRF52 as LoRa radio extenders.

### Architecture

NEXUS is **Reticulum-style**: the protocol is a library, not tied to specific hardware.

```
┌──────────────┐  BLE   ┌──────────────┐  LoRa   ┌──────────────┐
│  Android App │◄──────►│   ESP32 +    │◄───────►│  Other nodes │
│  (NEXUS node │        │   SX1262     │         │  (ESP32/nRF/ │
│   via JNI)   │        │ (NEXUS node) │         │   phone/RPi) │
└──────────────┘        └──────────────┘         └──────────────┘
       │ BLE                                            │ BLE
       ▼                                                ▼
┌──────────────┐                                ┌──────────────┐
│ Other phones │                                │   RPi node   │
│  (BLE mesh)  │                                │  (Python UI) │
└──────────────┘                                └──────────────┘
```

- **Phone** runs a full NEXUS node (libnexus compiled for ARM64 via Android NDK + JNI)
- **ESP32/nRF52** runs a full NEXUS node with LoRa radio -- operates independently OR as radio bridge
- **BLE** connects phone ↔ phone and phone ↔ ESP32 (extends to LoRa range)
- **Each device has its own identity, routing, crypto** -- no device is "dumb"

### 8a. Production Firmware (ESP32 + nRF52)

**RadioLib HAL bridge** -- connect SX1262 to NEXUS LoRa transport:
- `firmware/common/radiolib_hal.cpp` -- implements `nx_lora_radio_ops_t` using RadioLib
  - `radio_send(data, len)` → `radio.transmit(data, len)`
  - `radio_recv(buf, len, timeout)` → `radio.receive(buf, len)` with timeout
  - `radio_cad()` → `radio.scanChannel()` for collision avoidance
  - `radio_set_sf(sf)` → `radio.setSpreadingFactor(sf)` for ASF integration
  - `radio_rssi()` / `radio_snr()` for link quality metrics

**BLE serial bridge** -- phone ↔ ESP32 tunnel:
- `firmware/common/ble_bridge.cpp` -- BLE GATT service for NEXUS packet tunneling
  - TX characteristic: phone sends packets to ESP32's LoRa transport
  - RX characteristic: ESP32 forwards received LoRa packets to phone via notify
  - Uses NUS (Nordic UART Service) profile for compatibility
  - Auto-reconnect, MTU negotiation (max 512B)

**Identity persistence**:
- ESP32: NVS (Non-Volatile Storage) for identity keys
- nRF52: Flash storage for identity keys
- First boot generates identity, subsequent boots load from flash

**OLED display** (Heltec V3):
- Show: node address, neighbor count, message count, RSSI
- Cycle pages on button press

**Updated platformio.ini** -- include all NEXUS sources (fragment, session, group, anchor, ASF)

### 8b. Android App (Kotlin + Jetpack Compose)

**JNI bridge** (`app/src/main/cpp/`):
- `nexus_jni.cpp` -- JNI wrapper around libnexus C API
- CMakeLists.txt compiles libnexus + monocypher for ARM64/ARM32/x86_64
- Exposes: Node, Identity, Session, Group to Kotlin via JNI

**Core Kotlin layer** (`app/src/main/java/.../nexus/`):
- `NexusNode.kt` -- Kotlin wrapper around JNI, manages node lifecycle
- `NexusService.kt` -- Android foreground service, keeps node running
- `BleTransport.kt` -- Android BLE central, connects to ESP32 or other phones
- `MessageStore.kt` -- Room database for message history
- `ContactStore.kt` -- Known identities / nicknames

**UI screens** (Jetpack Compose):
1. **Chat list** -- conversations with contacts, unread badges
2. **Chat** -- send/receive messages (session-encrypted), show delivery status
3. **Groups** -- create group, add members, group chat
4. **Mesh status** -- neighbor map, route table, transport stats, RSSI
5. **Settings** -- identity (show/export/import), node role, beacon interval
6. **Device pairing** -- scan for ESP32 via BLE, pair for LoRa extension

**Notifications**: incoming message alerts via Android notification channel

### 8c. RPi Web Dashboard (bonus, lightweight)

- `tools/dashboard/` -- Python HTTP server + static HTML/JS
- Zero dependencies (stdlib only)
- Same features as Android but in a browser
- Useful for headless RPi gateway nodes

### Build instructions

```sh
# Firmware (requires PlatformIO)
cd firmware/heltec_v3 && pio run -t upload

# Android (requires Android Studio + NDK)
cd android && ./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk

# RPi dashboard
cd tools/dashboard && python3 server.py
# Open http://localhost:8080
```

---

## Project Structure

```
retprot/
├── CMakeLists.txt              # Top-level build
├── PLAN.md                     # This file
├── lib/
│   ├── CMakeLists.txt          # libnexus static library
│   ├── include/nexus/          # Public headers
│   │   ├── types.h             # Core types, constants, error codes
│   │   ├── platform.h          # Platform abstraction interface
│   │   ├── identity.h          # Key generation, address derivation
│   │   ├── packet.h            # 13-byte header serialize/deserialize
│   │   ├── crypto.h            # AEAD, key exchange, ephemeral mode
│   │   ├── transport.h         # Transport vtable + registry
│   │   ├── announce.h          # Ed25519-signed identity announcements
│   │   ├── route.h             # PRISM routing engine
│   │   ├── node.h              # Node lifecycle + event loop
│   │   ├── lora_radio.h        # LoRa radio HAL + mock radio
│   │   ├── ble_radio.h         # BLE radio HAL + mock radio
│   │   ├── fragment.h          # Message fragmentation & reassembly
│   │   ├── anchor.h            # ANCHOR store-and-forward mailbox
│   │   ├── session.h           # Double Ratchet linked sessions
│   │   ├── group.h             # Group sender-key encryption
│   │   ├── halow.h             # WiFi HaLow transport API
│   │   └── lora_asf.h          # Adaptive spreading factor API
│   ├── src/                    # Implementation
│   │   ├── identity.c
│   │   ├── packet.c
│   │   ├── crypto.c
│   │   ├── transport.c
│   │   ├── announce.c
│   │   ├── route.c
│   │   ├── fragment.c
│   │   ├── anchor.c
│   │   ├── session.c
│   │   ├── group.c
│   │   ├── node.c
│   │   └── platform/
│   │       ├── platform_posix.c   # Linux/macOS/RPi
│   │       ├── platform_esp32.c   # ESP32-S3 (FreeRTOS)
│   │       └── platform_nrf52.c   # nRF52840 (Zephyr)
│   ├── test/                   # Unit tests (17 test suites)
│   │   ├── CMakeLists.txt
│   │   ├── test_identity.c     # 7 tests
│   │   ├── test_packet.c       # 9 tests
│   │   ├── test_crypto.c       # 8 tests
│   │   ├── test_announce.c     # 7 tests
│   │   ├── test_route.c        # 17 tests
│   │   ├── test_lora.c         # 11 tests
│   │   ├── test_fragment.c     # 9 tests
│   │   ├── test_anchor.c       # 7 tests
│   │   ├── test_session.c      # 11 tests
│   │   ├── test_session_node.c # 6 tests
│   │   ├── test_ble.c          # 7 tests
│   │   ├── test_group.c        # 9 tests
│   │   ├── test_gateway.c      # 8 tests
│   │   ├── test_halow.c        # 16 tests
│   │   ├── test_asf.c          # 14 tests
│   │   ├── test_stress.c       # 6 stress benchmarks
│   │   └── test_stress_1k.c    # 8 tests (1024 nodes)
│   └── vendor/
│       └── monocypher/         # Monocypher 4.0.2
├── transports/
│   ├── serial/
│   │   └── nexus_serial.c      # Serial/UART transport
│   ├── tcp/
│   │   └── nexus_tcp.c         # TCP transport
│   ├── lora/
│   │   ├── nexus_lora.c        # LoRa transport + mock radio
│   │   └── nexus_lora_asf.c    # Adaptive spreading factor
│   ├── ble/
│   │   └── nexus_ble.c         # BLE transport + mock radio
│   ├── wifi/
│   │   └── nexus_halow.c       # WiFi HaLow (802.11ah) transport
│   └── pipe/
│       └── nexus_pipe.c        # In-memory pipe transport (testing)
├── bindings/
│   └── python/
│       ├── nexus/              # Python package
│       │   ├── __init__.py     # Public API
│       │   ├── _ffi.py         # ctypes FFI definitions
│       │   ├── errors.py       # Error handling
│       │   ├── identity.py     # Identity management
│       │   ├── transport.py    # Transport helpers
│       │   └── node.py         # Node class
│       ├── cli.py              # CLI tool
│       └── tests/              # Python tests (17 total)
│           ├── test_identity.py  # 4 tests
│           ├── test_node.py      # 5 tests
│           ├── test_group.py     # 4 tests
│           └── test_session.py   # 4 tests
└── firmware/
    ├── heltec_v3/              # Heltec WiFi LoRa 32 V3
    │   ├── platformio.ini
    │   └── src/main.cpp
    └── rak4631/                # RAK4631 WisBlock Core
        ├── platformio.ini
        └── src/main.cpp
```

## Build & Test

```sh
# C library + tests
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure

# Python bindings tests
cd bindings/python/tests
PYTHONPATH=.. python3 -m unittest discover -v
```

---

## Phase 9: Messenger Feature Parity -- COMPLETE

**Goal**: Persistent storage, delivery receipts, rich media, QR codes, and group chat UI.

### Sub-Phase 9a: C Library Enhancements
- **Propagation flag enforcement**: `handle_data()` in node.c checks NX_MSG_FLAG_PROPAGATE and stores to anchor even when route exists
- **Message signatures**: `nx_msg_sign()` / `nx_msg_verify()` with Ed25519 (NX_FIELD_SIGNATURE=0x12, 67B TLV)
- **Read receipt builder**: `nx_msg_build_read()` parallel to ACK builder
- **Tests**: 7 new C tests (signature roundtrip, tampered, wrong key, unsigned, propagate flag, read receipt, buffer overflow)
- **Python**: sign/verify via ctypes, FieldType.SIGNATURE, 5 new Python tests

### Sub-Phase 9b: Android Room Database
- Room entities: MessageEntity, ConversationEntity, ContactEntity
- DAOs with Flow queries for reactive UI
- MessageRepository wraps DAOs, handles conversation upsert
- Replaces in-memory conversations with persistent SQLite
- Nickname migration from SharedPreferences to Room

### Sub-Phase 9c: Delivery Receipts + NXM Integration
- Kotlin NXM parser/builder (NxmTypes, NxmParser, NxmBuilder)
- NexusService dispatches by NXM type: TEXT (auto-ACK), ACK (→DELIVERED), READ (→READ)
- Delivery status indicators in chat bubbles (hourglass/check/double-check/blue/X)
- `message.c` added to Android CMakeLists.txt (was missing)

### Sub-Phase 9d: Rich Media + QR + Location
- QR code generation/scanning via ZXing (nexus:// URL format)
- Location sharing with osmdroid MapView
- MediaBubble composable for IMAGE/FILE/VOICE_NOTE types
- CAMERA permission in manifest

### Sub-Phase 9e: Group Chat Management UI
- JNI: 5 group methods + on_group callback wired to C library
- Room: GroupEntity, GroupMemberEntity, GroupDao (DB version 2)
- GroupConversationScreen: group chat with sender name per bubble
- GroupInfoScreen: member list, add member dialog
- ChatScreen: group section with create/delete dialogs
- NexusService: createGroup, sendGroupMessage, deleteGroup, onGroup handler
