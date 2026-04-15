# Deploying NEXUS

Three targets: the Linux/RPi daemon, embedded firmware (ESP32-S3 or
nRF52840), and the Android app. Each runs a full NEXUS node — no
master/slave relationship — so pick whichever combination you need.

## 1. Linux / Raspberry Pi (nexusd)

### Build

```sh
cd /path/to/retprot
mkdir -p build && cd build
cmake .. && make -j$(nproc)
ctest --output-on-failure
```

Artefacts of interest:

- `build/app/nexusd` — daemon binary
- `build/libnexus.so` — shared library (for Python bindings / JNI)
- `bindings/python/cli.py` — `nexus-cli` tool

### Run

```sh
./build/app/nexusd                        # zero-config LAN (UDP multicast)
./build/app/nexusd -p node.example:4242   # add an Internet peer
./build/app/nexusd -n -l 4242             # TCP only, no multicast
```

Identity persists at `~/.nexus/identity` (0600). Role defaults to GATEWAY
so the daemon bridges across every transport it brings up.

Useful flags:

| Flag          | Meaning                                                       |
|---------------|---------------------------------------------------------------|
| `-l PORT`     | TCP listen port (default 4242)                                |
| `-p HOST:PORT`| Outbound TCP peer (repeatable, max 16)                        |
| `-i FILE`     | Identity file path                                            |
| `-r ROLE`     | 0=leaf 1=relay 2=gateway 3=anchor                             |
| `-u SOCK`     | UDS control socket (default `/tmp/nexusd.sock`)               |
| `-k HEX`      | 64-hex PSK; enables mutual-auth on TCP-inet                   |
| `-A IP`       | Add IP to inbound allow-list (repeatable, exact match)        |
| `-n`          | Disable UDP multicast                                         |
| `-v`          | Verbose logging                                                |

### Controlling a running daemon

```sh
nexus-cli status
nexus-cli send AABBCCDD "hello"
nexus-cli inbox
nexus-cli announce
```

Pass `--sock /path/to/other.sock` if you changed `-u`.

### systemd unit (example)

```ini
[Unit]
Description=NEXUS mesh daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=nexus
ExecStart=/usr/local/bin/nexusd -r 2 -l 4242
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## 2. Pillar (public relay) setup

A **pillar** is any `nexusd` reachable on the open Internet with a static
port. Clients behind NAT connect outbound; the pillar accepts.

Minimum:

```sh
./build/app/nexusd -r 5 -l 4242 -k $(openssl rand -hex 32)
```

Guidance:

- Run with role 5 (PILLAR) so store-and-forward tiering applies the correct
  buffer limits.
- Always set a PSK with `-k`; connections from mismatched peers are
  dropped before any mesh byte is exchanged.
- Open only the configured TCP port; no UDP or ICMP is required.
- If operator policy requires it, additionally gate accepts with `-A`
  allow-list entries. This does **not** restrict outbound connects.
- Back up `~/.nexus/identity` — if it's lost, every peer stored against
  your short address must re-add the pillar.

Clients add the pillar in their config (same `-p host:port -k ...` flags
as the pillar uses for PSK).

## 3. Firmware (ESP32-S3 / nRF52840)

Firmware lives under `firmware/`. Build with PlatformIO:

```sh
cd firmware
pio run -e heltec_v3        # ESP32-S3 + SX1262
pio run -e rak4631          # nRF52840 + SX1262
pio run -e xiao_nrf52840    # Seeed XIAO nRF52840 + Wio SX1262
pio run -e <env> -t upload  # flash
pio device monitor          # USB serial at 115200
```

Per-board init lives in `firmware/<env>/src/main.cpp`; the RadioLib HAL
bridge is a symlink to `firmware/common/radiolib_hal.cpp` so a fix lands
on every board at once.

Identity persists to NVS (ESP32) or InternalFS (nRF52) and is generated
on first boot. Boot LED sequence: red (boot), blue (radio init), green
(ready). Rapid red blinking indicates a fatal init error.

## 4. Android

```sh
cd android
./gradlew assembleRelease
adb install app/build/outputs/apk/release/app-release.apk
```

The CI build (see `.github/workflows/`) produces signed-debug APKs on
every tag. Features of note:

- SharedPreferences-backed identity (auto-Base64 exported in settings).
- UDP multicast + TCP-inet client baked in; Pillar list in Settings.
- Room FTS4 search across every conversation.
- Paper-message QR export/import for fully offline bridging.

## 5. Upgrading

- `libnexus` preserves the 13-byte header and extended-header type space
  across minor versions; watch `lib/include/nexus/types.h` for additions.
- The Android Room schema is bumped when entities change
  (`fallbackToDestructiveMigration`), so installed devices wipe chat
  history on version bumps. Ship an explicit migration before 1.0.
- Firmware OTA is not yet implemented; plan flash-over-USB rollouts.

## 6. Operations checklist

- [ ] Identity file backed up (`~/.nexus/identity`, Android settings export).
- [ ] PSK set on every pillar and every client that uses it.
- [ ] `nexusd -v` logs sampled at least once after deploy.
- [ ] Anchor mailbox size matches node role (see `docs/PROTOCOL.md` §12).
- [ ] Firewalls permit the configured TCP ports; UDP/4243 if multicast is
      wanted.
- [ ] Android devices have battery-optimisation exception for
      `com.nexus.mesh` (the app prompts on first run).
