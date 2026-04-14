# NEXUS Firmware — Debug Notes

After several rounds of fixes (pin mappings, SoftDevice v7.3.0, board variant, global-ctor
elimination, boot-LED diagnostics, SPI init order) the MCU firmware still does not come up
reliably on XIAO nRF52840, XIAO ESP32-S3, Heltec V3, and RAK4631. This doc captures what
we've already addressed and the remaining root-cause hypotheses, ranked by likelihood.

---

## 0. What already works

| Fix | Commit | Status |
|-----|--------|--------|
| BLE init before LoRa on nRF52 | `802f6a0` | ✅ visible in `nRF Connect` |
| SX1262 pin mappings / SPI bus / TCXO / RXEN | `5e3fc49` | ✅ verified against WIO-SX1262 schematic |
| XIAO nRF52840 board variant added to Adafruit BSP | `fb66c99` | ✅ variant.cpp.o links, pin map correct |
| SoftDevice S140 v7.3.0 target | `8ff0749` | ✅ matches Adafruit bootloader |
| Boot diagnostic LEDs on XIAO | `a64e928` | ✅ 3×BLUE boot, N×RED per step |
| RadioLib `Module`/`SX1262` as **static locals** not globals | — | ✅ no pre-Arduino hardfault |

Variant is confirmed built (symlinked by `link_sources.py` into the BSP variants dir and
compiled each build). Node init does not touch the transport registry, so init order is safe.

---

## 1. PRIMARY SUSPECT — Blocking LoRa receive

`firmware/common/radiolib_hal.cpp` → `rl_receive()`:

```cpp
int state = rl->receive(buf, buf_len);   // blocks ~100 s, ignores timeout_ms
```

This is the synchronous overload. It spins inside RadioLib up to the module's internal
timeout (~100 s for SF9/BW250) and **ignores the `timeout_ms` argument** that `nx_node_poll`
passes in. Net effect:

- main loop stalls for tens of seconds at a time
- no beacons/announces sent (user sees "nothing happens")
- BLE bridge appears connected but nothing flows
- watchdog (if enabled) may reset the chip, producing the "random reboot" reports

### Fix

Switch to the non-blocking pattern:

```cpp
static volatile bool s_rx_flag = false;
static void IRAM_ATTR rl_dio1_isr() { s_rx_flag = true; }

// in init:
rl->setPacketReceivedAction(rl_dio1_isr);
rl->startReceive();

// in rl_receive():
uint32_t t0 = millis();
while (!s_rx_flag) {
    if (millis() - t0 >= timeout_ms) return NX_ERR_TIMEOUT;
    yield();                       // let BLE/FreeRTOS breathe
}
s_rx_flag = false;
int n = rl->getPacketLength();
int st = rl->readData(buf, (n < (int)buf_len) ? n : (int)buf_len);
rl->startReceive();                // re-arm
```

This also allows `nx_node_poll(node, 50)` to cooperatively multitask with BLE and
announce scheduling.

---

## 2. SECONDARY — RF switch conflict

`radiolib_hal.cpp` calls `rl->setDio2AsRfSwitch(true)` during init. XIAO/Heltec boards with
**external RXEN** (SKY13373 or similar) then call `rl->setRfSwitchTable()` from
`main.cpp` **afterwards**. RadioLib's latest semantics let only one path win — the later call
overrides, but DIO2 is still toggling on TX which fights the antenna switch.

### Fix

- On boards that use external RXEN (XIAO + WIO-SX1262, Heltec V3 WIO variant, RAK4631):
  remove/guard the `setDio2AsRfSwitch(true)` call in `radiolib_hal.cpp` and let the board
  `main.cpp` install its own `setRfSwitchTable()`.
- On boards where DIO2 **is** the RF switch (bare SX1262 modules): keep the DIO2 call and
  skip the external table.

Best done via a board-level flag, e.g. `#define NX_LORA_RF_SWITCH_EXTERNAL 1` in
`platformio.ini` `build_flags` for WIO-style boards.

---

## 3. Region mismatch

`NX_LORA_CONFIG_DEFAULT` = 915 MHz / BW 250 kHz / SF9. EU devices on 868 MHz boards will
init successfully (the radio accepts the freq) but sit silently outside the ISM band, so no
neighbors ever hear each other. This looks identical to "firmware broken" from the user side.

### Fix

Add region build flag:

```ini
# platformio.ini per env
build_flags = -DNX_LORA_REGION_EU868=1   ; or US915 / AU915 / AS923 / IN865
```

Honor it in `nx_lora_config_default()` or a new `nx_lora_config_for_region()` helper.

---

## 4. Serial.begin timing (native USB)

On XIAO nRF52840 and ESP32-S3 native-USB boards, `Serial` is a TinyUSB CDC device and
`Serial.begin(115200)` **does not block** until host opens the port. Early log lines are
silently dropped. This doesn't cause "firmware doesn't work" on its own — but makes
debugging look like the firmware hung before printing, misleading earlier iterations.

### Fix (debug builds only)

```cpp
Serial.begin(115200);
#if defined(NX_WAIT_FOR_SERIAL)
uint32_t t0 = millis();
while (!Serial && (millis() - t0) < 3000) { delay(10); }
#endif
```

Gate on a `-DNX_WAIT_FOR_SERIAL=1` build flag — never enable in production, it will hang
battery-powered nodes waiting for a host.

---

## 5. Boot-LED step map (XIAO nRF52840)

Set by `a64e928`. Read this counting **RED** blinks after the 3 startup BLUE blinks:

| RED blinks | Step reached | If you stop here |
|-----------:|--------------|------------------|
| 1 | Settings (NVS/InternalFS) loaded | InternalFS mount failing, check partition |
| 2 | Identity loaded / generated | Monocypher RNG / file I/O issue |
| 3 | BLE stack up (Bluefruit.begin) | SoftDevice mismatch, check v7.3.0 .hex flashed |
| 4 | `nx_node_init_with_identity` | Heap exhausted — shrink NX_* tables in platformio.ini |
| 5 | SPI + Module + SX1262 static locals constructed | SPI pins wrong, or `radio_ptr` NULL |
| 6 | LoRa transport registered + active | `rl->begin()` returned non-zero, check wiring/TCXO |
| GREEN solid | Ready | — |

If only the 3 BLUE boot blinks show and **no RED**, the SoftDevice crashed before `setup()`.
Re-flash the Adafruit nRF52840 bootloader and confirm `softdevice: "s140"` / `"version": "7.3.0"`
in `boards/seeed_xiao_nrf52840.json`.

---

## 6. Verification checklist after fixes

1. `pio run -e xiao_nrf52840` builds clean.
2. After flash: 3×BLUE → (fast progression) → solid GREEN within 2 s.
3. `nRF Connect` sees the device with NUS service.
4. Android app connects, requests config (magic `0xFFFFFFCF` + 0x01), receives 25-byte response.
5. Two devices powered on within LoRa range → each shows the other under Neighbors within 30 s.
6. Message send from Android A → received on Android B via LoRa gateway.

---

## 7. What to do next

In order:

1. Patch `rl_receive()` to non-blocking (Section 1) — single biggest win.
2. Add `NX_LORA_RF_SWITCH_EXTERNAL` guard (Section 2).
3. Add region build flag (Section 3).
4. Re-test on XIAO nRF52840 first (most instrumented), then ESP32-S3, then Heltec/RAK.

Do NOT revisit pin mappings, SoftDevice version, or global constructors — those are solved.
