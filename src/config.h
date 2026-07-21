// config.h — compile-time constants for smalltv-mod
//
// Hardware: SmallTV-ultra (GeekMagic original) — ESP-12F (ESP8266) driving a
// 1.54" 240x240 ST7789 IPS panel. The board-specific pin map + panel quirks
// live in board_smalltv_ultra.h, included below. (ESP32-C2 and NM-TV-154
// variants were dropped in 3.0.0.)
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
// FW_VERSION is NOT defined here. tools/gen_version.py (a PlatformIO pre-build
// script) derives it from `git describe --tags` at build time and injects it
// as -DFW_VERSION='"x.y.z"'. To release: `git tag v3.0.3 && pio run`.
#define FW_NAME      "swc-digital"
#define REPO_OWNER   "night-sornram"
#define REPO_NAME    "swc-digital"
#define UPDATE_ASSET "swc-digital-smalltv-ultra.bin"
#define REPO_URL     "https://github.com/" REPO_OWNER "/" REPO_NAME
#define GH_API_HOST  "api.github.com"

// ---------------------------------------------------------------------------
// Display wiring + panel quirks — board-specific, pulled from the right header.
// Provides TFT_SCLK/MOSI/DC/RST/CS/BL, TFT_BGR, TFT_BL_DEFAULT_INVERTED,
// HAS_LDR/LDR_PIN/ADC_MAX. Both panels are 1.54" 240x240 ST7789 IPS.
// ---------------------------------------------------------------------------
// SmallTV-ultra only (ESP-12F / ESP8266). The multi-board target-selection
// macro was removed in 3.0.0 when ESP32-C2 and NM-TV-154 support was dropped.
#include "board_smalltv_ultra.h"

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ---------------------------------------------------------------------------
// Limits (bound RAM usage on the ESP8266)
// ---------------------------------------------------------------------------
#define MAX_WIFI_NETS     4    // saved WiFi networks; strongest visible wins at boot
#define MAX_URL_LEN     200    // webhook base URL

// ---------------------------------------------------------------------------
// UI modes (3.0.0)
// ---------------------------------------------------------------------------
// Three UI modes for the usage display. AUTO rotates between CODEX and ZAI.
// Named UiMode to avoid clashing with the DisplayMode renderer base class in
// Mode.h (that class stays the polymorphic render interface used by main.cpp).
enum UiMode : uint8_t {
  MODE_CODEX   = 0,
  MODE_ZAI     = 1,
  MODE_AUTO    = 2,
  MODE_SYSTEM  = 3,
  MODE_VITALS  = 4,
  MODE_WEATHER = 5,
};
#define DEFAULT_MODE  MODE_AUTO

// ---------------------------------------------------------------------------
// Compile-time feature toggles. WITH_USAGE gates the usage meter (and the mDNS
// name it advertises); a lean build sets -D WITH_USAGE=0 in a PlatformIO env.
// (Ticker/Radar toggles were removed in 3.0.0 along with those features.)
// ---------------------------------------------------------------------------
#ifndef WITH_USAGE
#define WITH_USAGE 1
#endif

// ---------------------------------------------------------------------------
// Usage display (3.0.0)
// ---------------------------------------------------------------------------
#define USAGE_STALE_AFTER_MS    180000UL   // mark STALE after 180 s without a push
#define USAGE_AUTOROTATE_SEC    30         // AUTO: dwell on each provider
#define USAGE_AUTOROTATE_MIN    2
#define USAGE_AUTOROTATE_MAX    3600

// Palette (RGB565). Match the spec exactly.
// Each value computed from the #RRGGBB via ((R&0xF8)<<8)|((G&0xFC)<<3)|(B>>3).
#define USAGE_COLOR_CODEX       0x150F   // #10A37F: (0x10&0xF8)<<8|(0xA3&0xFC)<<3|(0x7F>>3) = 0x1000|0x0500|0x0F
#define USAGE_COLOR_ZAI         0x6B1F   // #6C63FF: (0x6C&0xF8)<<8|(0x63&0xFC)<<3|(0xFF>>3) = 0x6800|0x0300|0x1F
#define USAGE_COLOR_BG          0x0883   // #081018: (0x08&0xF8)<<8|(0x10&0xFC)<<3|(0x18>>3) = 0x0800|0x0080|0x03
#define USAGE_COLOR_CARD        0x10E4   // #111C26: (0x11&0xF8)<<8|(0x1C&0xFC)<<3|(0x26>>3) = 0x1000|0x00E0|0x04
#define USAGE_COLOR_TEXT        0xF7BF   // #F4F7FA: (0xF4&0xF8)<<8|(0xF7&0xFC)<<3|(0xFA>>3) = 0xF000|0x07A0|0x1F
#define USAGE_COLOR_MUTED       0x8D16   // #8EA1B2: (0x8E&0xF8)<<8|(0xA1&0xFC)<<3|(0xB2>>3) = 0x8800|0x0500|0x16
#define USAGE_COLOR_WARN        0xFD84   // #FFB020: (0xFF&0xF8)<<8|(0xB0&0xFC)<<3|(0x20>>3) = 0xF800|0x0580|0x04
#define USAGE_COLOR_CRIT        0xF28A   // #F05252: (0xF0&0xF8)<<8|(0x52&0xFC)<<3|(0x52>>3) = 0xF000|0x0280|0x0A
#define USAGE_COLOR_STALE       0x63B0   // #677786: (0x67&0xF8)<<8|(0x77&0xFC)<<3|(0x86>>3) = 0x6000|0x03A0|0x10

// ---------------------------------------------------------------------------
// Defaults (used on first boot / factory reset)
// ---------------------------------------------------------------------------
#define DEFAULT_AP_SSID      "SmallTV-Setup"
#define DEFAULT_AP_PASS      ""              // empty => open AP
#define DEFAULT_HOSTNAME     "smalltv"
#define DEFAULT_POLL_SEC      120            // how often to refresh data
#define TICKER_RETRY_SEC       12            // fast retry after a failed/skipped fetch
#define TICKER_RETRY_MAX        4            // consecutive fast retries before backing off
#define DEFAULT_ROTATE_SEC    10             // how long each symbol is shown
#define DEFAULT_RANGE        "1d"            // chart timeframe (e.g. 1d/5d/1mo/1y)
#define DEFAULT_POINTS        48             // sparkline points requested
#define DEFAULT_BRIGHTNESS    90             // 0..100 %
#define DEFAULT_HTTP_TIMEOUT  8000           // ms per request

// --- Clock / night mode (device-wide) ---
#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.nist.gov"
#define DEFAULT_TZ_NAME         ""        // IANA display name; empty = UTC
#define DEFAULT_TZ_POSIX        "UTC0"    // POSIX TZ rule the device feeds SNTP
#define DEFAULT_NIGHT_ENABLED   false
#define DEFAULT_NIGHT_START_MIN 1320      // 22:00
#define DEFAULT_NIGHT_END_MIN   420       // 07:00
#define DEFAULT_NIGHT_LEVEL     0         // 0..100, 0 = backlight fully off

// Night-mode NTP trust: only ENTER night mode when the clock was confirmed by a
// successful NTP sync within NIGHT_NTP_TRUST_MS (else we assume the clock may be
// wrong and keep the screen on). While inside the window but unconfirmed, re-arm
// SNTP every NIGHT_NTP_RESYNC_MS until a fresh sync lands or the window ends
// (morning). Once night mode has switched on, it stays on until the window ends.
#define NIGHT_NTP_TRUST_MS      300000UL  // 5 min: max age of the sync that unlocks night
#define NIGHT_NTP_RESYNC_MS      30000UL  // re-sync attempt cadence while held off
