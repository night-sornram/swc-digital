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

## USB clock (`clock_usb`)

See `USB_CLOCK.md` for the full build / flash / restore / CLI guide. The USB
clock is a separate firmware line and does not share the Wi-Fi usage code.

## Recovery

Before first flashing a device, make a 4 MB stock-flash backup and store it
outside the repo. Never commit it — it may contain saved Wi-Fi credentials.

## License

WTFPL (inherited from `giovi321/smalltv-mod`). The custom USB firmware and the
Wi-Fi usage display are isolated behind the `clock_usb` and `smalltv_ultra`
PlatformIO targets respectively.
