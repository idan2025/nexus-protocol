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

- Battery-optimization whitelist prompt (task #12).
- Delivery receipts UI — ACK NXM plumbing (task #13).
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
