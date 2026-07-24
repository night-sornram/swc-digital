# SWC Digital

Custom firmware for the GeekMagic SmallTV-ultra (ESP-12F / ESP8266, 1.54" 240×240 ST7789).

Two firmware targets, both for the same hardware:

- **`smalltv_ultra`** — Wi-Fi firmware that displays live Codex and z.ai usage.
  Three screen modes: `CODEX`, `Z.AI`, and `AUTO` (alternates every 30 s).
  A Mac service pushes usage data over the LAN every 60 s.
- **`clock_usb`** — USB-only recovery / reference firmware. No Wi-Fi, no cloud,
  configured entirely over the onboard CH340 serial bridge. See `USB_CLOCK.md`.

The recovery loader (`smalltv_ultra_loader`) is a tiny Wi-Fi + web-OTA image used
to slip a full firmware past a stock updater that rejects the full image for
lack of space.

## Quickstart (fresh Mac)

One command sets up everything — config, pairing, and the auto-starting
background service:

```sh
git clone https://github.com/night-sornram/swc-digital.git
cd swc-digital
uv run --python 3.11 --with zeroconf --with psutil tools/wifi_usage_service.py setup
```

`setup` walks you through: dependency check → config creation → device
pairing → LaunchAgent install → verification. The pairkey is stored in macOS
Keychain (never in a file). After it completes, usage data is pushed to the
device every 60 s and the service survives reboots.

**Prerequisites:** the device must already be flashed with the Wi-Fi firmware
(see [Build](#build) + [Flash](#flash) below) and booted into its Setup AP
(join `SmallTV-Setup` on your Mac before running `setup`).

Other commands (run individually if you prefer):

```sh
tools/wifi_usage_service.py pair --url http://192.168.4.1   # pair a device
tools/wifi_usage_service.py install                         # register the service
tools/wifi_usage_service.py uninstall                       # remove the service
tail -f ~/Library/Logs/swc-digital-wifi-usage.log           # watch it push
```

## Hardware

- MCU: ESP8266EX, 26 MHz crystal, 4 MB flash (DIO, 40 MHz).
- USB bridge: CH340/CH341.
- Display: ST7789, 240×240, SPI mode 3.
- Pin map: see `src/board_smalltv_ultra.h` (SCLK=14, MOSI=13, DC=0, RST=2, CS=15, BL=5).

Only SmallTV-ultra is supported. ESP32-C2 and NM-TV-154 support was removed in 3.0.0.

## Build

```sh
uvx --from platformio platformio run -e smalltv_ultra         # production
uvx --from platformio platformio run -e smalltv_ultra_loader  # recovery loader
uvx --from platformio platformio run -e clock_usb             # USB-only reference
```

## Flash

```sh
uvx --from esptool esptool \
  --port "$(uv run --with pyserial tools/clockctl.py find-port)" --baud 115200 \
  write-flash 0x0 .pio/build/smalltv_ultra/firmware.bin
```

Always use 115200 baud. The CH340 auto-reset wiring resets the ESP8266 each time
a serial session opens.

## Usage display (the Wi-Fi firmware)

The device exposes:

- `POST /api/usage` — the Mac service pushes one body per provider here.
- `GET /api/usage` — read both providers' snapshots.
- `_aiusage._tcp` mDNS service for discovery.
- A web UI on port 80 for mode / brightness / Wi-Fi / OTA settings.

See `docs/superpowers/plans/` for the design and `tools/wifi-usage.toml.example`
for the Mac service config.

## Mac usage service

`tools/wifi_usage_service.py` polls Codex (`~/.codex/auth.json`) and z.ai
(`~/.claude/settings.json`) every 60 s and pushes each provider to every
SmallTV-ultra it discovers on the LAN. Per-provider backoff on 429/5xx. Never
logs tokens, headers, or full responses.

Install as a LaunchAgent using `tools/com.night.swc-digital-wifi-usage.plist.example`.

## Pairing (3.1+)

Every SmallTV-ultra is locked to one Mac + one browser/phone. The first time:

1. The device boots into a `SmallTV-Setup` Wi-Fi AP with a random 12-char
   password shown on its physical screen.
2. Join that Wi-Fi on your Mac.
3. On the Mac:
   ```sh
   uv run --no-project python tools/wifi_usage_service.py pair --url http://192.168.4.1
   ```
   The command prints a 16-character pairkey **once**. Save it in your
   password manager — it is stored in macOS Keychain, not in any file.
4. Open `http://192.168.4.1/` in a browser. The browser prompts for a
   username + password. Use `admin` + the pairkey.
5. Configure your home Wi-Fi in the WebUI. The device reboots into STA mode.

## Recovery (after Wi-Fi change or lost key)

1. Factory-reset the device (hold the reset pin 10 s, or use the WebUI's
   Factory Reset button while still paired). It boots into `SmallTV-Setup`
   with a fresh random AP password.
2. Join the AP, then:
   ```sh
   uv run --no-project python tools/wifi_usage_service.py recover --url http://192.168.4.1
   ```
   `recover` is identical to `pair` (it generates a new key + pairs) but
   documents that you are replacing a lost/old pairing. The old pairkey in
   Keychain is overwritten.

## Migration from 3.0.x

OTA a 3.1 image onto a 3.0.x device. The device will boot into the
`SmallTV-Setup` AP (the schema v4 migration forces a re-pair — your 3.0.x
config is preserved, but the device no longer trusts the old unauthenticated
connection). Re-run the Pairing steps above. Your Wi-Fi credentials, display
settings, and usage history are kept.

## USB clock (`clock_usb`)

See `USB_CLOCK.md` for the full build / flash / restore / CLI guide. The USB
clock is a separate firmware line and does not share the Wi-Fi usage code.

## Recovery (stock flash backup)

Before first flashing a device, make a 4 MB stock-flash backup and store it
outside the repo. Never commit it — it may contain saved Wi-Fi credentials.

## License

WTFPL (inherited from `giovi321/smalltv-mod`). The custom USB firmware and the
Wi-Fi usage display are isolated behind the `clock_usb` and `smalltv_ultra`
PlatformIO targets respectively.
