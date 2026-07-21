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
#define FW_NAME      "swc-digital"
#define FW_VERSION   "3.0.0"
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
// Display mode — what the device shows
//   1 = Claude usage meter (5h/7d usage bars, fed by the usage backend)
//   3 = carousel: rotate through the ticked features on a timer
//   (Stocks/Radar modes 0 and 2 were removed in 3.0.0; Plan 2 redefines the
//   mode enum. DEFAULT_MODE points at MODE_USAGE so config.h alone compiles.)
// ---------------------------------------------------------------------------
#define MODE_USAGE     1
#define MODE_CAROUSEL  3
#define DEFAULT_MODE   MODE_USAGE
#define DEFAULT_CAROUSEL_SEC 30      // per-mode dwell in carousel

// ---------------------------------------------------------------------------
// Compile-time feature toggles. WITH_USAGE gates the usage meter (and the mDNS
// name it advertises); a lean build sets -D WITH_USAGE=0 in a PlatformIO env.
// (Ticker/Radar toggles were removed in 3.0.0 along with those features.)
// ---------------------------------------------------------------------------
#ifndef WITH_USAGE
#define WITH_USAGE 1
#endif

// Claude usage mode: once data stops arriving for this long (PC asleep, daemon
// stopped, network down) the screen switches from the stats to the idle mascot
// animation. Effective timeout also scales with the poll period (see main.cpp).
#define USAGE_STALE_GRACE_MS  20000UL

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
