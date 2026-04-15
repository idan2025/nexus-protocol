# NEXUS — Cross-Session Progress Log

This file is a **pick-up-where-you-left-off** tracker. Update it every time a
non-trivial task starts, flips status, or uncovers something the next
session needs to know. Claude sessions lose conversation state but keep this
file, so the rule is: **if in doubt, write it down here.**

Order: newest on top, one section per work item, status tags in brackets.

Shorthand:
- `[todo]` not started
- `[wip]` in progress (name the file(s) being touched)
- `[blocked: reason]`
- `[done YYYY-MM-DD]`

---

## Now in flight

### Firmware: session resume across reboots `[done 2026-04-15, unverified]`
- Core serialize/deserialize already landed in `0558bc6`
  (`lib/src/session.c` + `nx_session_store_serialize/deserialize`).
- `firmware/common/session_store.{h,cpp}`: platform-conditional persistence
  of `nx_session_store_t`. ESP32 uses NVS (`Preferences`, namespace
  `nexus_sess`, one blob per slot `s0..s15`). nRF52 uses InternalFS
  (`/nxs_0..`) via Adafruit LittleFS. Save iterates all slots; invalid
  slots are erased from flash so the file set tracks valid sessions.
- Wired into all four firmware targets (`heltec_v3`, `rak4631`,
  `xiao_esp32s3`, `xiao_nrf52840` — the heltec wiring was already staged
  from the prior session, the other three are new this session):
  `nx_session_store_load(&node.sessions)` after the anchor-load block in
  `setup()`, and `nx_session_store_save(&node.sessions)` in `loop()` when
  `nx_session_count(&node.sessions)` changes. Save-on-count-change (not
  per-message) keeps flash wear bounded — ratchet keys evolve every
  message but we only snapshot when a new handshake completes or a peer
  is forgotten. Trade-off: if the device reboots between a handshake and
  the next send, the last few messages replay as skipped keys on the
  other side, which the existing skip-ahead code already handles.
- `firmware/common/link_sources.py` globs `*.cpp`/`*.h` in `common/` and
  symlinks them into each project's `src/`, so no `platformio.ini`
  changes were needed.
- SECURITY NOTE (also in header): flash contents of `nexus_sess` are as
  sensitive as the identity key. Physical flash dump enables decryption
  of past ciphertext captured off-air. Encrypt-at-rest is a later task.
- Closes P1 "Link/session resume across reboots (persisted ratchet
  state)" from `docs/FEATURES.md` §4.
- Not yet verified on hardware; needs a flash, handshake, power-cycle,
  and confirm the peer's next message decrypts.

### Firmware: region-aware default LoRa frequency `[done 2026-04-14]`
- `lib/include/nexus/lora_radio.h`: `NX_LORA_CONFIG_DEFAULT` now pulls
  `frequency_hz` from a compile-time-selectable `NX_LORA_DEFAULT_FREQ_HZ`
  macro driven by region tokens: `NX_LORA_REGION_US915` (default),
  `_EU868`, `_EU433`, `_CN470`, `_AS923`, `_IN865`, `_AU915`, `_KR920`,
  `_TH923`. Channel frequencies match Meshtastic "LongFast" mid-slot
  (e.g. EU868=869.525MHz, US915=906.875MHz) to stay clear of LoRaWAN
  gateway low-edge listening.
- Build as `pio run -e xiao_nrf52840 -- -DNX_LORA_REGION_EU868` or add
  to `platformio.ini build_flags`. All existing builds default to US915
  (no behavior change for existing users). Runtime override via BLE
  config / nexus-cli still works as before.
- Desktop rebuild passes (`cmake .. && make`).

### Android: Delivery receipts wiring `[done 2026-04-14, unverified]`
- Most infra was already in place: `NxmBuilder.buildAck` / `buildRead`, auto-ACK
  on TEXT receive (`NexusService.handleNxmMessage` NxmType.TEXT path), ACK/READ
  handlers updating `deliveryStatus` via `repository.updateDeliveryStatus`, and
  `ChatBubble` in `ConversationScreen.kt` rendering all 5 states (⏳ / ✓ / ✓✓ /
  ✓✓ blue / ✗). The gap was that READ receipts were never *sent*.
- Added (this session):
  - `MessageDao.getUnreadIncomingForPeer(peerAddr, readStatus)` and
    `markIncomingRead(peerAddr, readStatus)`.
  - `MessageRepository.getUnreadIncomingForPeer()` / `markIncomingRead()`.
  - `NexusService.sendReadReceipts(peerAddr)` — iterates unreceipted incoming
    messages with an NXM msgId, sends `buildRead(msgId)` to the peer (session
    first, fallback to plaintext), then flips local `deliveryStatus = READ` and
    clears the conversation unread count.
  - `ConversationScreen.kt` — `LaunchedEffect(peerAddr, messages.size)` calls
    `service.sendReadReceipts(peerAddr)` on open and whenever new messages
    arrive while the user is viewing.
- Semantics: we reuse `deliveryStatus` on incoming rows as a "did we already
  receipt this" flag. Values < READ mean "need to send receipt", READ means
  "already sent". Outgoing rows continue to use the field as
  SENDING→SENT→DELIVERED→READ.
- Out of scope: group messages use sender-keys (no per-message receipts),
  GroupConversationScreen intentionally omits the indicator.
- Not yet verified on hardware. CI will type-check on next release tag.

### Android: Identity backup &amp; restore `[done 2026-04-14, unverified]`
- Implemented entirely in Kotlin — no JNI/libnexus changes needed. Identity
  bytes come from existing `node.getIdentityBytes()`, encryption uses stock
  Android crypto (PBKDF2-HMAC-SHA256 600k iters + AES-256-GCM).
- Files added/changed:
  - NEW `android/app/src/main/java/com/nexus/mesh/data/IdentityBackup.kt` —
    encrypt/decrypt helpers. Wire: `[magic(4)][ver(1)][iters(4)][salt(16)][iv(12)][ct+tag]`,
    Base64-wrapped. Throws `BadBackupException` / `BadPassphraseException`.
  - `service/NexusService.kt` — added `exportIdentity(passphrase)` and
    `importIdentity(blob, passphrase)`. Import stops node, writes new bytes to
    `PREFS_NAME`, restarts node via `startNode()`.
  - `ui/SettingsScreen.kt` — Export / Import buttons inside the Identity card,
    plus `ExportIdentityDialog` (passphrase + confirm, 8+ chars) and
    `ImportIdentityDialog` (paste blob, passphrase, "replace" checkbox, inline
    error).
- NOT yet verified on hardware or by Gradle build (no local gradle installed).
  Will be validated by the next release tag (CI runs `gradle assembleDebug`).
- Known follow-ups: QR export/import, identity rotation screen, biometric
  gate before showing blob.

## Up next (P0 Android)

- File &amp; image attachments (chunked `nx_node_send_large`).
- Voice-note recorder (AMR-WB or Opus → NXM VOICE_NOTE field).
- Pillar inbox pull — needs new protocol exthdr first.

## Up next (Pillar)

- Inbox pull protocol: new `NX_EXTHDR_INBOX_REQ` / `INBOX_RESP` types, mailbox
  query API, server-side replay of stored packets addressed to the requester.
- Per-peer rate limiting.
- `/metrics` HTTP endpoint (Prometheus format).

---

## Recently completed

### Android: Battery optimization prompt `[done 2026-04-14, unverified]`
- `AndroidManifest.xml`: added `REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` permission.
- `MainActivity.kt`: on first run, if `PowerManager.isIgnoringBatteryOptimizations()`
  is false and the user hasn't already dismissed the prompt, shows an
  `AlertDialog`. "Allow" launches `ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS`
  (falls back to `ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS` on OEMs that
  hide it). Dismissal stored in `PREFS_STARTUP` under `battery_opt_dismissed`.

### v0.3.0 released `[done 2026-04-14]`
- Tag `v0.3.0` pushed, release CI queued. Run `gh run list --workflow=release.yml`.
- Once green, flasher at `https://idan2025.github.io/nexus-protocol/` auto-picks
  firmware with the non-blocking RX fix.

### Firmware: non-blocking LoRa RX `[done 2026-04-14]`
- `firmware/common/radiolib_hal.cpp`: `rl->receive()` was blocking ~100 s and
  ignoring `timeout_ms` → main loop stalled, no beacons, appeared dead.
- Replaced with `startReceive()` + DIO1 ISR + `readData()` pattern. TX parks
  RX to standby and re-arms after.
- Gated `setDio2AsRfSwitch(true)` behind `!NX_LORA_RF_SWITCH_EXTERNAL`. XIAO
  nRF52840 + RAK4631 now set `-DNX_LORA_RF_SWITCH_EXTERNAL=1` because their
  `main.cpp` installs an external `setRfSwitchTable()`.
- Verified builds: `xiao_nrf52840` (RAM 17.8%, Flash 30.4%), `xiao_esp32s3`
  (RAM 39.8%, Flash 30.5%).
- Not tested on hardware yet.

### Pillar server `[done 2026-04-14]`
- `app/pillard.c` — dedicated public-Internet relay binary.
- `scripts/nexus-pillar.service` — hardened systemd unit.
- `scripts/README-pillar.md` — install + federation docs.

### Docs `[done 2026-04-14]`
- `docs/FEATURES.md` — full P0/P1/P2 backlog (Android, Pillar, firmware, protocol, CLI, testing).
- `docs/FIRMWARE_DEBUG.md` — root-cause analysis of the RX blocking bug + LED step map.
- `docs/index.html` footer links to the docs above and Pillar README.

---

## Known gotchas for future sessions

- `docs/FEATURES.md` is the master backlog. Always cross-reference before
  picking the next task — update priority tags there if reality disagrees.
- Android uses Monocypher via JNI; there is no Argon2 in the vendored Monocypher
  4.0.2 build. For passphrase KDFs use either Argon2 from a separate source
  (adds ~20 KB flash) or a stretched BLAKE2b (lower cost, weaker).
- Firmware v0.2.0 and earlier had the blocking-RX bug. Anyone testing against
  a v0.2.0 flash will see "firmware doesn't work" — always point them at
  v0.3.0+.
- `radiolib_hal.cpp` uses file-scope `s_rx_flag` / `s_rx_armed`. Single-radio
  assumption; multi-radio needs a trampoline.
- Tag names: `v{major}.{minor}.{patch}`. Pushing a tag matching `v*` triggers
  `.github/workflows/release.yml` which builds all firmware + Linux + Android
  and cuts a release. Don't tag casually.
- `MEMORY.md` (auto-memory) contains architecture cheat-sheets. PROGRESS.md
  (this file) is for per-task state. Keep them separate.

## When starting a new session

1. Read this file top-to-bottom.
2. Read `MEMORY.md` (Claude auto-loads it) for architecture context.
3. Check `gh run list --limit 5` to see if last release CI finished.
4. Check `git log --oneline -10` for commits since last session.
5. Pick the top `[wip]` item or the first `[todo]` in "Up next".
