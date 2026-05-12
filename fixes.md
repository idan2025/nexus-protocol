# Android App — Bug & Improvement Punch List

Focused review of the Android client, looking for real bugs and low-risk
improvements that won't break existing behavior.

## Real bugs

### 1. BLE GATT handle leak on peer-side disconnect
**Where:** `android/app/src/main/java/com/nexus/mesh/ble/BleTransport.kt`,
`gattCallback.onConnectionStateChange` (line ~400).

**Symptom:** `STATE_DISCONNECTED` clears `rxChar`, the state flows, and
sets `_connected.value = false`, but it **never calls `g.close()`**. The
explicit `disconnect()` function does it correctly. Auto-disconnects
(peer drops, OS kill) leak a `BluetoothGatt` instance per cycle. Android
caps GATT handles at ~7–32 per process; after a long session BLE
silently fails with `GATT_INSUFFICIENT_RESOURCES`.

**Fix:** call `g.close(); gatt = null` in the disconnect branch.

### 2. Chunked-receiver allocation DoS
**Where:** `NexusService.maybeHandleChunkedMedia` (line ~1075).

**Symptom:** `arrayOfNulls(total)` where `total` is the attacker-controlled
`partTotal` (u16, up to 65535). A single peer can force a 256-KB pointer-
table allocation per fake `msgId`; with `chunkBuffers` unbounded, that's
amplifiable.

**Fix:** cap `partTotal` to 512 (1 MB ÷ 2 KB chunks). Also cap the total
number of concurrent in-flight chunked transfers across all peers.

### 3. Unused single-message media builders are an ACK footgun
**Where:** `NxmBuilder.kt` — `buildImage`, `buildFile`, `buildVoice` still
exist after the chunked-transfer refactor.

**Symptom:** They generate their own internal `MSG_ID` via
`generateMsgId()` and are still callable. If anything regresses to them,
the sender's DB row and the receiver's ACK won't agree on the id and the
delivery tick will never turn green.

**Fix:** delete the unused builders so callers can't accidentally reach
them; everything now goes through `buildChunk()`.

### 4. `NexusNode.isRunning` is racy across native callbacks
**Where:** `NexusNode.kt`.

**Symptom:** Plain `var isRunning`, no `@Volatile`. The poll thread runs
on Dispatchers.IO; native callbacks (`onData`, `onSession`, `onNeighbor`,
`onGroup`) fire from JNI threads. A `stop()` between poll and callback
can deliver an `onData` after `isRunning = false`.

**Fix:** `@Volatile var isRunning`.

### 5. Stale chunked transfers live forever if no follow-up chunk arrives
**Where:** `NexusService.gcStaleChunkBuffers` is only called inline
inside `maybeHandleChunkedMedia`.

**Symptom:** If a transfer stops mid-flow and no other chunks ever come
in, the buffer sits in memory for the lifetime of the service.

**Fix:** run `gcStaleChunkBuffers()` from the `pollJob` loop every ~30 s.

## Worth fixing soon

### 6. Voice recorder UI overruns `setMaxDuration(60_000)`
**Where:** `ConversationScreen.kt` `VoiceRecorderDialog`.

**Symptom:** `MediaRecorder` stops emitting audio at 60 s; no
`setOnInfoListener` is registered, so the dialog timer keeps counting
and Stop-after-60s reads an underfilled file.

**Fix:** register `MEDIA_RECORDER_INFO_MAX_DURATION_REACHED` listener
that auto-stops the recorder and triggers send.

### 7. Notifications collide across conversations
**Where:** `NexusService.showMessageNotification`.

**Status:** **Already correct** — `mgr.notify(from.hashCode(), …)` is in
place. Review was wrong on this item.

### 8. UTF-8 fallback in `onData/onSession` inserts garbage as messages
**Where:** `NexusService.kt` lines ~912, ~948, ~1042.

**Symptom:** If parsing as NXM fails, the raw bytes get inserted as a
plaintext message. That was literally the "pillar gibberish" the user
saw. Any future C-side decryption gap will surface the same way.

**Fix:** gate the fallback on a printable-ASCII heuristic; drop binary
garbage instead of inserting it.

### 9. `UnspecifiedRegisterReceiverFlag` warning is suppressed
**Where:** `NexusService.kt:362`.

**Status:** **Already correct** — `RECEIVER_NOT_EXPORTED` is used on
Tiramisu+; the `@Suppress` only applies to pre-API-33 where the flag
doesn't exist. Review was wrong on this item.

## Noted but not changed

These would benefit the app but aren't in the "fix without breaking"
scope — flagged here for visibility.

- **TOFU / pubkey verification UI.** Anyone with a stolen identity blob
  can impersonate. A SafeFlinger-style fingerprint verification
  workflow would close that.
- **`broadcastNicknameToNeighbors` rate-limit.** Rapid name-saves in
  Settings could fire one send per neighbor per save. A 1 s debounce
  would cap the burst.

## Status

- v0.6.27 / v0.6.28 / v0.6.29 / v0.6.30 already shipped.
- Items 1–9 above land in the next release.
