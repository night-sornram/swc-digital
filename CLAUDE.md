# Claude session hand-off — SWC Digital

Read `AGENTS.md` and `USB_CLOCK.md` before editing, building, or flashing.
They are the source of truth for hardware safety, recovery, and user commands.

## Current product

This repository carries two firmware lines for the same SmallTV-ultra hardware:

- **`smalltv_ultra`** (3.0.0) — Wi-Fi firmware that displays live Codex and
  z.ai usage. Three modes: `CODEX`, `Z.AI`, `AUTO` (flips every 30 s). A Mac
  service (`tools/wifi_usage_service.py`) polls `~/.codex/auth.json` and
  `~/.claude/settings.json` every 60 s and pushes each provider to every
  device on the LAN over `_aiusage._tcp` mDNS. After 180 s with no push, the
  active provider shows STALE. The firmware stores NO provider token — only the
  resulting percentages cross the LAN.
- **`clock_usb`** — USB-only recovery / reference firmware for the same
  hardware. No Wi-Fi, no cloud, configured over the onboard CH340 serial
  bridge. It must never be given Wi-Fi credentials or cloud dependencies.

The two lines are isolated behind their PlatformIO targets; do not confuse
them. See `docs/superpowers/plans/` for the `smalltv_ultra` spec.

### v3.1.0 — secure pairing (in progress on `feature/v3.1-secure-pairing`)

`smalltv_ultra` 3.1.0 locks each device to one Mac via HTTP Digest
(RFC 2617, qop=auth, username `admin`, 16-char Crockford-Base32 pairkey).
The pairkey lives only in macOS Keychain (`com.night.swc-digital.device-<id>`),
never in a file. The device stores only `MD5(admin:realm:pairkey)` (H1).
Every protected route (`/`, `/api/*`, `/update`) requires Digest; only
`/api/identity` and captive-portal probes are open. A factory reset or OTA
from 3.0.x boots the device into `SmallTV-Setup` AP and forces a re-pair.
The Mac service uses `wifi_usage_service.py run` (and `pair`/`recover`
subcommands); on 401/403 it pauses 15 min, not the 5s network retry. mDNS
selects the device by its stable Device ID (TXT `id`); a duplicate ID fails
closed. See AGENTS.md "Rollout (3.1.0)" for the 9-step home procedure.

### USB clock modes (`clock_usb`)

The USB clock has six modes; the Face mode is flashed, USB-verified, and
visually confirmed on the physical display:

- **Face**: two cyan LED-style eyes on black with partial-region blink/look
  animation and Auto, Neutral, Happy, Focus, Curious, Sleepy, Alert, and
  Celebrate moods. Face selection and mood persist across USB resets.
- **Usage**: six one-column bars for C1–C3 Claude session usage and X1–X3
  Codex weekly usage, including active-account, recommendation, trend, and
  sync indicators.
- **BTC**: current BTC/USD price, 24-hour change, and 24 normalized one-hour
  candles. The Mac fetches public Coinbase Exchange data; the clock uses no key.
- **Clock**: large local time supplied by the Mac over USB.
- **Thai**: visually verified Noto Sans Thai test view showing `สวัสดี`.
- **Gallery**: up to seven local 240 × 240 images with a 3–300 second slideshow.
  Photos and slideshow state live only in the clock's LittleFS partition.
- **Reminders**: up to eight EEPROM-backed daily alerts. A verified alert stays
  steady for about 60 seconds and then returns to the previous screen.

V2-A is flashed and installed. `tools/clock_service.py` owns one persistent
serial connection, proxies normal CLI/GUI/Gallery operations through a user-only
Unix socket, and supplies RAM-only Auto emotions from Mac idle time. Stable
serial ownership, socket CLI routing, cached state, and `auto_emotion=focus` are
verified. The user visually confirmed the V2-A transitions and animation.

Run the local GUI with:

```sh
uv run --with pyserial --with pillow tools/clock_gui.py
```

It listens only on `127.0.0.1:8765`. The GUI is the normal path for gallery
uploads and slideshow control; do not replace it with a network service.
Its forms use a per-process anti-CSRF token, image requests are bounded, and all
actions restore cached RAM-only display data after the USB-open reset. Gallery
uploads use a checksum-verified temporary file before replacing a slot.

## Usage automation

### Wi-Fi push (`smalltv_ultra`)

`tools/wifi_usage_service.py` runs as a LaunchAgent
(`tools/com.night.swc-digital-wifi-usage.plist.example`), polling Codex and
z.ai every 60 s and pushing each provider to every SmallTV-ultra discovered on
the LAN. Per-provider backoff on 429/5xx. Its private `tools/wifi-usage.toml`
is Git-ignored. Never print OAuth tokens, account identifiers, Authorization
headers, or full provider responses. Structured logs carry only provider name,
timestamp, HTTP status, and error category.

### USB collector (`clock_usb`)

The private `tools/usage-collector.toml` and its state file are Git-ignored.
Never print their contents, OAuth tokens, Keychain values, provider stderr, or
account identifiers. The collector rotates one Claude account per five-minute
run because Claude's undocumented endpoint rate-limits requests; it caches the
other Claude values. Codex profiles refresh every run. The Usage GUI action
publishes cached Claude values and refreshes Codex only.

The stable ignored helper is `tools/.bin/claude_profile_vault`. Keychain prompts
require the user's macOS login password; only the user should enter it.

## Safe device workflow

1. Preserve the dirty worktree and each device's private stock backup.
2. Build the target you intend to flash (`smalltv_ultra`, `smalltv_ultra_loader`,
   or `clock_usb`) and run `git diff --check` plus `esptool image-info`.
3. Before a flash, verify the SHA-256 of that device's private stock backup.
4. Flash only at 115200, then verify the written-data hash and (for `clock_usb`)
   a USB response.
5. Ask the user to visually confirm every visual change. A passing build or
   serial response alone is not visual confirmation.

Opening the serial port resets the clock. Restore time in the same connection
before sending dynamic data; `clockctl.py` and the collector already do this.
The collector caches the preceding complete usage values and sends them as
optional `USAGE` history so trend arrows survive those resets. A fresh collector
state may contain `null` Claude slots while the first three rotations complete;
that partial baseline is valid and must remain loadable.
Gallery slideshow playback persists through those resets, while Clock and Usage
are intentionally RAM-only. BTC market data is RAM-only, but BTC and Face screen
selection persist so collector resets return to the selected screen. Face also
persists its selected mood.

Do not commit, push, publish, erase flash, restore stock firmware, or alter
user credentials unless the user explicitly asks.
