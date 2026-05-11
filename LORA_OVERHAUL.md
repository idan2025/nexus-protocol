# LoRa & Communication Overhaul — pre-v0.7.0

Tracker for the punch list compiled from the v0.6.19 audit.
Status legend: [ ] pending · [x] applied · [-] deferred.

## Baseline at start of overhaul
- `ctest`: 22/22 pass (mock-radio LoRa + all protocol layers correct in isolation).
- Symptom in production: XIAO ESP32S3 + Heltec V3 never see each other as
  neighbors via LoRa, despite TCXO fix, NVS-preserving update flow, and
  both boards configured to 917.8 MHz.
- Conclusion: bug lives at the firmware↔radio boundary or in policies
  (duty cycle, announce cadence). Mock radio masks all of these.

## Fixes

### 1. Duty cycle silently throttles announces
- [x] `transports/lora/nexus_lora.c:60-72,137`
- 1% cap (= 600 ms per 60 s window). One announce at SF9/BW250 ≈ 349 ms
  → second announce in the same minute is blocked → at most ~1 announce
  per minute escapes the cap. RREQ / route TX also eat the budget.
- US915 (default region) has no duty cycle, only per-channel dwell time.
  The cap is enforced everywhere regardless of region.
- Fix: introduce `NX_LORA_DUTY_CYCLE_PCT` build flag, default 0 (off).
  Skip the gate when pct == 0.

### 2. First announce delayed 30 s
- [-] (no change needed) `lib/src/node.c:553-555`
- `last_announce_ms` starts at 0; the periodic check fires the first
  announce only at `+beacon_interval_ms`. User sees "no neighbors" for
  30 s after every boot.
- Fix attempted via `last_beacon_ms == 0` sentinel; that broke 3
  integration tests because the harness doesn't expect any TX on the
  first poll. Reverted -- discovered while reviewing that every
  firmware main.cpp already calls `nx_node_announce()` once directly
  after node init (see heltec_v3 main.cpp:991, xiao_esp32s3:606,
  xiao_nrf52840:698, rak4631:458), so the boot-time announce was
  already happening. The reason "no neighbors" persisted was fix 1
  (duty cycle silently throttled all subsequent announces). No code
  change required for this item.

### 3. Synchronous TX blocks BLE & FreeRTOS
- [x] `firmware/common/radiolib_hal.cpp:124`
- `rl->transmit()` is blocking for full airtime (~350 ms at SF9). On
  ESP32-S3 + NimBLE this holds the core long enough for GATT writes to
  time out and is watchdog-risky on long packets.
- Fix: convert to `startTransmit()` + DIO1-flag poll (mirrors the RX
  pattern already in place). Use `setPacketSentAction()` for the ISR
  if RadioLib exposes it cleanly, otherwise multiplex on DIO1 with a
  state flag.

### 4. No TX retry on transient radio errors
- [x] `firmware/common/radiolib_hal.cpp:130-132`
- Single `-706` (TCXO timeout) or `-16` (param error) returns `NX_ERR_IO`
  and the packet is silently lost — no resend at LoRa layer or above.
- Fix: retry up to 2× with short backoff on transient codes, then
  surface failure.

### 5. Busy-wait backoff
- [x] `transports/lora/nexus_lora.c:47-54`
- `delay_ms()` spins on `nx_platform_time_ms()`; on FreeRTOS / Zephyr
  this starves other tasks.
- Fix: call `nx_platform_sleep_ms()` (already exists in the platform layer).

### 6. XIAO ESP32S3 has no explicit `SPI.begin()`
- [x] `firmware/xiao_esp32s3/src/main.cpp`
- Heltec V3 calls `SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS)`.
  XIAO relies on RadioLib's internal `SPI.begin()` + board variant
  defaults. If variant defaults are off, init silently fails.
- Fix: mirror Heltec's pattern with WIO-SX1262 pins (GPIO 7/8/9, CS=4).

### 7. No LoRa TX/RX logging
- [x] `firmware/common/radiolib_hal.cpp`
- When a user reports "no comms", there is no way to tell whether the
  chip is even attempting TX, what the RSSI floor looks like on RX,
  or where in the path things break down.
- Fix: add `[LORA] TX seq=N len=L err=E (Nms)` and `[LORA] RX len=L
  rssi=R snr=S` Serial prints behind a `NX_LORA_VERBOSE` build flag
  (default ON for debug builds; can be left ON until v0.7.0).

### 8. `nx_settings_t` has magic but no version field
- [x] `lib/include/nexus/lora_radio.h` / `firmware/common/settings_store.{h,cpp}`
- If a future commit adds a field to `nx_settings_t`, old NVS data loads
  with the magic still matching → struct read as garbage.
- Fix: add `uint32_t version` after magic; bump on layout change; reset
  to defaults on mismatch (don't pretend to migrate; flags-only struct).

### 9. CAD retry exhaustion still transmits
- [x] `transports/lora/nexus_lora.c:140-153`
- If the channel reports busy on all 5 retries, the loop falls through
  and we transmit anyway → guaranteed collision.
- Fix: return `NX_ERR_BUSY` after exhaustion; caller (announce / route)
  can drop or defer to the next tick.

### 10. XIAO ESP32S3 global `SX1262 radio` constructor
- [x] `firmware/xiao_esp32s3/src/main.cpp:81`
- Works on ESP32 because Arduino-ESP32 runs main() after static ctors,
  but diverges from Heltec / RAK / XIAO-nRF52 which all construct in
  `setup()` as static-locals. Cosmetic only; not a blocker.
- Fix: move Module + SX1262 into `setup()` as static locals.

## Out-of-scope for this overhaul
- Cross-platform RNG quality audit (deferred to Tier 4 of the original
  audit plan).
- Tier 2 protocol correctness audit (fragment edges, ratchet desync,
  gateway loops).
- Tier 3 security hygiene (constant-time, replay windows).
