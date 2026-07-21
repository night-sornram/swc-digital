# SWC Digital 3.0.0 — Plan 1: Repository Consolidation (SmallTV-ultra only)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse the repo to a single hardware target (SmallTV-ultra / ESP8266), delete the Ticker / Radar / Claude-mascot / ESP32-C2 / NM-TV-154 source lines, and rename the ESP8266 env to `smalltv_ultra` — so that Plan 2 can build a clean usage-display firmware on top.

**Architecture:** Three PlatformIO environments survive: `smalltv_ultra` (the Wi-Fi firmware, renamed from `smalltv`), `smalltv_ultra_loader` (renamed from `smalltv_loader`), and `clock_usb` (USB-only recovery firmware, untouched). The multi-board `Platform/Gfx/OtaUpdate/config` abstraction collapses to a single ESP8266 code path; the three `board_*.h` files collapse to one `board_smalltv_ultra.h`. Feature folders `ticker/`, `radar/`, and the Claude mascot halves of `usage/` are deleted. Settings loses the ticker/radar/usage-URL slices (kept fields are reused in Plan 2). This plan does NOT touch `usb_clock.cpp`, the USB tools (`clockctl.py`, `clock_gui.py`, `clock_service.py`, `usage_collector.py`, `crypto_market.py`, `codex_usage.py`, `claude_usage.py`, `claude_profile_vault.swift`, `extract_mascot.py`), the USB TOML/plist examples, or `tests/`.

**Tech Stack:** PlatformIO `espressif8266`, ArduinoJson 7, GFX Library for Arduino, ESP8266 LittleFS, ESP8266WebServer, ESP8266 mDNS.

**Branch:** `feature/v3-usage-display` (already created from `main`).

**Spec source:** `pasted-text-20260721-155906-89253a9e.txt` §1 (Make repository SmallTV-ultra only) and §2 (Delete Ticker, Radar and Claude UI). Plan 2 owns §3 (Usage domain) and §4 (Settings migration); Plan 3 owns §5 (Mac service); Plan 4 owns docs/CI/release.

---

## Pre-flight (do once, before Task 1)

- [ ] **P1: Confirm you are on `feature/v3-usage-display` and clean enough to work.**

```sh
git branch --show-current       # must print: feature/v3-usage-display
git status --short              # if dirty: stash or commit user-owned changes first (AGENTS.md forbids reset/checkout to clean)
```

Expected: branch name as above. If `git status` shows files you did not create, STOP and ask the user — AGENTS.md says the worktree may be dirty and existing changes are user-owned.

- [ ] **P2: Save the current dirty diff as a patch BEFORE deleting `RadarMode` (spec §build gates).**

```sh
git diff > docs/superpowers/plans/_preserve-dirty-diff.patch
git status --short >> docs/superpowers/plans/_preserve-dirty-diff.patch
```

The patch file is a safety net only; it is never committed. If `git diff` is empty, the file will just contain the status line — that is fine.

- [ ] **P3: Capture the baseline build so we can compare footprint at the end.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -20 | tee docs/superpowers/plans/_baseline-smalltv-build.txt
uvx --from esptool esptool image-info .pio/build/smalltv/firmware.bin 2>&1 | tee docs/superpowers/plans/_baseline-smalltv-image-info.txt
```

Record the `RSFWD`/size line and the `Free/Used RAM` line from the build, and the image size from image-info. These two `.txt` files are reference scratch only — do not commit them.

---

## File Structure (what changes in this plan)

**Deleted (whole files):**
- `src/features/ticker/TickerMode.{h,cpp}`, `StockClient.{h,cpp}`, `StockData.h`
- `src/features/radar/RadarMode.{h,cpp}`, `RadarClient.{h,cpp}`, `RadarData.h`
- `src/features/usage/Mascot.{h,cpp}`, `mascot_frames.h`
- `src/board_esp32.h`, `src/board_esp32c2.h`
- `partitions/smalltv_4mb_ota.csv` (ESP32-only partition table)
- `n8n/` directory (Ticker webhook automation)
- `quotes-config.json` (Ticker quote keys)
- `.github/scripts/fetch-quotes.mjs` (dead Ticker proxy)

**Renamed:**
- `src/board_esp8266.h` → `src/board_smalltv_ultra.h`

**Modified:**
- `platformio.ini` — envs collapse to `smalltv_ultra`, `smalltv_ultra_loader`, `clock_usb`; default = `smalltv_ultra`; drop pioarduino URL, ESP32 partition table, ESP32 upload command
- `src/Platform.h` / `src/Platform.cpp` — delete `SMALLTV_ESP32C2`/`SMALLTV_ESP32` branches; keep only the ESP8266 path; keep `platformChipId`, `platformResetInfo`, `platformMakeSecureClient`, `platformTimeBegin`, `platformMdnsUpdate`, `platformAnalogWriteInit`, `platformMaxFreeBlock`, `platformFreeContStack`, `platformTimeValid`, `platformOnTimeSync`, `platformSetHostname`, `platformScanIsOpen`, `platformUpdateError`
- `src/Gfx.{h,cpp}` — delete the `#ifdef SMALLTV_ESP32*` panel-power branch (`TFT_PWR_PIN`), keep the ESP8266 ST7789 path
- `src/OtaUpdate.{h,cpp}` — delete the ESP32 `otaUpdateFromGitHub` stub branch (keep the ESP8266 boot-time flow intact)
- `src/WebPortal.cpp` — delete `features.{ticker,radar}` booleans (keep `features.usage`); delete `/api/refresh` route (`stocksForceRefresh`); the `clawdmeter` mDNS TXT stays for now (Plan 2 renames it)
- `src/config.h` — set `FW_NAME`, `FW_VERSION`, `REPO_OWNER`, `REPO_NAME`, `UPDATE_ASSET` (per spec §firmware identity); delete `MODE_STOCKS`/`MODE_RADAR`/`MODE_CAROUSEL`+`DEFAULT_MODE` will happen in Plan 2 (this plan only deletes `WITH_TICKER`, `WITH_RADAR`, the Ticker source enums/hosts, Radar enums/hosts/defaults, and ticker/radar/usage constants that are now unused); drop `board_esp{32,32c2}.h` include branches
- `src/Settings.{h,cpp}` — delete `SymbolCfg`, `Airport`, `TickerSettings`, `RadarSettings` structs; delete `Settings.ticker`, `Settings.radar`, `Settings.carouselTicker/carouselRadar`; keep `Settings.usage` (Plan 2 redefines it); keep all WiFi/AP/hostname/mode/carouselSec/display/clock fields
- `src/main.cpp` — delete `WITH_TICKER`/`WITH_RADAR` include guards and registry entries; keep `WITH_USAGE` for now (Plan 2 swaps the usage impl); simplify `carouselHas` to only the usage case; the carousel code stays (Plan 2 will replace it with AUTO rotation)
- `src/Net.cpp` — no change in this plan (the `clawdmeter` mDNS block is guarded by `WITH_USAGE` which still == 1)
- `src/webui.h` — delete the `<section class="tab" id="ticker">` block, the `<section class="tab" id="radar">` block, and their nav buttons; delete the Usage-tab `usageUrl` field (Plan 2 adds the new usage UI); delete ticker/radar JS branches

**Untouched (per spec — do NOT modify):**
- `src/usb_clock.cpp`, `src/thai_greeting_bitmap.h`, `src/loader.cpp` (only renamed env in platformio.ini)
- `tools/clockctl.py`, `tools/clock_gui.py`, `tools/clock_service.py`, `tools/usage_collector.py`, `tools/crypto_market.py`, `tools/codex_usage.py`, `tools/claude_usage.py`, `tools/claude_profile_vault.swift`, `tools/extract_mascot.py`, `tools/usage-collector.toml.example`, `tools/com.example.smart-weather-clock-usage.plist.example`
- `tests/`
- `src/Clock.{h,cpp}`, `src/Net.{h,cpp}` (Net impl unchanged here), `src/BearSslTuning.cpp`
- All top-level docs (`README.md`, `AGENTS.md`, `CLAUDE.md`, `USB_CLOCK.md`, `SECURITY.md`, `REMINDER_HANDOFF.md`) — Plan 4 owns these
- `docs/` Astro site — Plan 4 deletes it

---

## Task 1: Rename the ESP8266 board header and collapse config.h board selection

**Files:**
- Rename: `src/board_esp8266.h` → `src/board_smalltv_ultra.h`
- Modify: `src/config.h:37-43` (board-include branch)
- Modify: `src/Gfx.cpp` (any `#include "board_esp8266.h"`)

- [ ] **Step 1.1: Rename the file with `git mv` (preserves history).**

```sh
git mv src/board_esp8266.h src/board_smalltv_ultra.h
```

- [ ] **Step 1.2: Replace the target-selection macro block in `src/config.h`.**

Find the block at `src/config.h:37-43` (it currently looks like `#if defined(SMALLTV_ESP32C2) ... #elif defined(SMALLTV_ESP32) ... #else #include "board_esp8266.h" #endif`). Replace the entire block with a single direct include:

```cpp
// SmallTV-ultra only (ESP-12F / ESP8266). The multi-board target-selection
// macro was removed in 3.0.0 when ESP32-C2 and NM-TV-154 support was dropped.
#include "board_smalltv_ultra.h"
```

- [ ] **Step 1.3: Update any direct include of the old name in Gfx.cpp.**

```sh
grep -nR "board_esp8266.h" src/ || echo "no other references"
```

If `src/Gfx.cpp` (or anything else) includes `board_esp8266.h`, change it to `board_smalltv_ultra.h`. The include is normally pulled in transitively via `config.h`, so this is likely a no-op — verify with the grep.

- [ ] **Step 1.4: Verify the project still compiles for `smalltv` (the env still exists; rename happens in Task 5).**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -8
```

Expected: `SUCCESS` (ignore `SyntaxWarning` from `elf2bin.py` per AGENTS.md — those are third-party tool warnings, not firmware build failures). If you see a compiler error about `board_esp8266.h` not found, re-check Step 1.2/1.3.

- [ ] **Step 1.5: Commit.**

```sh
git add src/board_smalltv_ultra.h src/config.h src/Gfx.cpp 2>/dev/null || git add src/board_smalltv_ultra.h src/config.h
git commit -m "refactor(v3): rename board_esp8266.h to board_smalltv_ultra.h

Single hardware target now; drop the target-selection macro."
```

---

## Task 2: Delete the ESP32 board headers and partition table

**Files:**
- Delete: `src/board_esp32.h`
- Delete: `src/board_esp32c2.h`
- Delete: `partitions/smalltv_4mb_ota.csv`
- Delete: `partitions/` directory if now empty

- [ ] **Step 2.1: Delete the two ESP32 board headers.**

```sh
git rm src/board_esp32.h src/board_esp32c2.h
```

- [ ] **Step 2.2: Delete the ESP32 partition table.**

```sh
git rm partitions/smalltv_4mb_ota.csv
rmdir partitions 2>/dev/null || true   # remove dir if empty; ignore if not
```

- [ ] **Step 2.3: Verify nothing else references them.**

```sh
grep -nR "board_esp32\|smalltv_4mb_ota" src/ platformio.ini || echo "clean"
```

Expected: `clean`. If `config.h` Step 1.2 was applied correctly, there are no remaining `board_esp32*` references. The partition table is referenced only in `platformio.ini` envs `smalltv_c2`/`smalltv_esp32`, which Task 5 deletes — leave those env lines alone for now or this grep will be noisy.

- [ ] **Step 2.4: Verify the build still passes for `smalltv`.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -8
```

Expected: `SUCCESS`.

- [ ] **Step 2.5: Commit.**

```sh
git commit -am "refactor(v3): drop ESP32-C2 and NM-TV-154 board headers and partition table"
```

(`-a` is safe here because only the two deletions are staged from Step 2.1/2.2 and there are no other unstaged changes you want to keep separate. If `git status` shows unrelated files, use explicit `git add` instead.)

---

## Task 3: Collapse Platform.h to the ESP8266 code path only

**Files:**
- Modify: `src/Platform.h` (158 lines — collapse the `#ifdef SMALLTV_ESP32*` branch)
- Modify: `src/Platform.cpp` (no logic change expected — verify)

- [ ] **Step 3.1: Read the current `src/Platform.h` fully.**

```sh
sed -n '1,160p' src/Platform.h   # or open in editor
```

The file has one big `#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)` branch (Platform.h:12-74) and an `#else` ESP8266 branch (h:76-147). The header doc-comment at the top should also be updated to drop the multi-board language.

- [ ] **Step 3.2: Rewrite `src/Platform.h` keeping only the ESP8266 branch.**

Delete the `#if defined(...) / #elif / #endif` scaffolding. Keep everything that was in the `#else` ESP8266 branch verbatim:
- `WebServerClass = ESP8266WebServer` (h:88)
- `SecureClient = BearSSL::WiFiClientSecure`, `NetClient = WiFiClient`, `TlsSession = BearSSLSession`
- `platformSetHostname`, `platformTimeBegin`, `platformMdnsUpdate` (= `MDNS.update()`), `platformAnalogWriteInit`, `platformScanIsOpen`, `platformUpdateError`, `platformChipId`
- `PlatformReset` struct (h:97-116) with the ESP8266 `epc/addr` fields
- `platformResetInfo()`
- `platformMakeSecureClient(rxBuf, session, txBuf, cheapCiphers)` (h:130-140)
- `platformMaxFreeBlock`, `platformFreeContStack`
- `platformTimeValid` (h:152), `platformOnTimeSync` (h:157)

Update the top-of-file doc comment to: `// Platform.h — SmallTV-ultra (ESP-12F / ESP8266) platform surface. Single target since 3.0.0.`

- [ ] **Step 3.3: Read `src/Platform.cpp` (30 lines) and confirm it has no `#ifdef SMALLTV_ESP32*`.**

```sh
grep -n "SMALLTV_ESP32\|ESP32\|esp32" src/Platform.cpp || echo "clean"
```

Expected: `clean`. Platform.cpp is just SNTP callback wiring — no change needed.

- [ ] **Step 3.4: Build to confirm.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -8
```

Expected: `SUCCESS`. A common failure here is a leftover `#include` for an ESP32-only IDF header — search and remove if the compiler complains.

- [ ] **Step 3.5: Commit.**

```sh
git add src/Platform.h src/Platform.cpp
git commit -m "refactor(v3): collapse Platform.h to the ESP8266-only path"
```

---

## Task 4: Collapse Gfx, OtaUpdate, and config.h to ESP8266-only

**Files:**
- Modify: `src/Gfx.cpp` — remove `TFT_PWR_PIN` / ESP32 panel-power branch
- Modify: `src/OtaUpdate.{h,cpp}` — remove the ESP32 `otaUpdateFromGitHub` stub
- Modify: `src/config.h` — set firmware identity per spec; remove ESP32 branches and now-unused Ticker/Radar enums

- [ ] **Step 4.1: Gfx — remove ESP32 panel-power handling.**

Open `src/Gfx.cpp` and find the `gfxBegin` function (around cpp:47-82). It currently has a branch like:

```cpp
#if defined(SMALLTV_ESP32) || defined(SMALLTV_ESP32C2)
  pinMode(TFT_PWR_PIN, OUTPUT);
  digitalWrite(TFT_PWR_PIN, LOW);   // active-low panel rail
#endif
```

Delete that branch (the ESP8266 has no `TFT_PWR_PIN`; the BL pin is driven via `gfxSetBrightness`). Keep the rest of `gfxBegin` (SPI init, `gfxSetBrightness(s.brightness, s.backlightInverted)`, `gfxSetRotation(s.rotation)`).

- [ ] **Step 4.2: OtaUpdate — delete the ESP32 stub.**

```sh
grep -n "SMALLTV_ESP32\|ESP32\|esp32" src/OtaUpdate.h src/OtaUpdate.cpp
```

Delete the `#if defined(SMALLTV_ESP32)` / `#else` scaffolding around `otaUpdateFromGitHub` (OtaUpdate.h:~33-41), leaving only the ESP8266 declarations: `otaCheckLatest`, `otaBootRequested`, `otaRequestBootUpdate`, `otaBootUpdate`, `otaTakeBootResult`. In the .cpp, delete the ESP32 stub body if present (it is likely a no-op `return false;`).

- [ ] **Step 4.3: config.h — firmware identity block.**

Replace the firmware identity block at `src/config.h:14-30` with:

```cpp
#define FW_NAME      "swc-digital"
#define FW_VERSION   "3.0.0"
#define REPO_OWNER   "night-sornram"
#define REPO_NAME    "swc-digital"
#define UPDATE_ASSET "swc-digital-smalltv-ultra.bin"
```

Keep `REPO_URL` as a derived `#define REPO_URL "https://github.com/" REPO_OWNER "/" REPO_NAME` if it exists; otherwise add it. Keep `GH_API_HOST` (`api.github.com`) and drop `DAEMON_URL` (it pointed at the upstream clawdmeter daemon; unused after mascot removal).

- [ ] **Step 4.4: config.h — remove Ticker and Radar constants.**

Delete the Ticker source enums and hosts (`SRC_*`, `DEFAULT_SOURCE`, Yahoo/cash.ch/GitHub hosts and paths, `GH_QUOTES_RXBUF`) at config.h:100-141. Delete the Radar enums and hosts (`RADAR_SRC_*`, `DEFAULT_RADAR_SRC`, `ADSB_HOST`, `MAX_AIRCRAFT`, `MAX_AIRPORTS`, `MAX_ICAO_LEN`, radar defaults) at config.h:149-168. Delete `MAX_SYMBOLS`, `MAX_SYMBOL_LEN`, `MAX_NAME_LEN`, `MAX_SPARK_POINTS` at config.h:51-56 (they are ticker-only). Keep `MAX_WIFI_NETS`, `MAX_URL_LEN`.

Delete `WITH_TICKER` and `WITH_RADAR` defines (config.h:78-86). Keep `WITH_USAGE` (== 1) — Plan 2 swaps its implementation but the define still gates mDNS.

Delete `MODE_STOCKS`, `MODE_RADAR` (keep `MODE_USAGE`, `MODE_CAROUSEL`, `DEFAULT_MODE` — Plan 2 redefines the mode enum, but deleting only STOCKS/RADAR now keeps the build alive).

- [ ] **Step 4.5: Build to confirm.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -15
```

Expected: this build will FAIL with errors in `TickerMode`, `RadarMode`, `Settings.cpp` (references to deleted `SRC_*`, etc.). That is expected — Tasks 5–7 fix those. The goal of this step is only to confirm there are no ESP32-related errors left in `Platform/Gfx/OtaUpdate/config.h` themselves. If the ONLY errors are in `src/features/{ticker,radar}/`, `Settings.{h,cpp}`, `main.cpp`, and `WebPortal.cpp`, proceed. If you see errors inside `Platform.{h,cpp}` / `Gfx.cpp` / `OtaUpdate.{h,cpp}` / `config.h`, fix them before moving on.

- [ ] **Step 4.6: Commit (the build is intentionally broken at the feature layer; subsequent tasks fix it).**

```sh
git add src/Gfx.cpp src/OtaUpdate.h src/OtaUpdate.cpp src/config.h
git commit -m "refactor(v3): collapse Gfx/OtaUpdate/config to ESP8266-only; set 3.0.0 identity

Build is broken at the feature layer until Ticker/Radar are deleted in Tasks 5-7."
```

---

## Task 5: Delete the Ticker and Radar feature modules

**Files:**
- Delete: `src/features/ticker/` (whole directory: `TickerMode.{h,cpp}`, `StockClient.{h,cpp}`, `StockData.h`)
- Delete: `src/features/radar/` (whole directory: `RadarMode.{h,cpp}`, `RadarClient.{h,cpp}`, `RadarData.h`)
- Delete: `n8n/` (whole directory — Ticker webhook automation)
- Delete: `quotes-config.json` (Ticker quote keys)
- Delete: `.github/scripts/fetch-quotes.mjs` (dead Ticker proxy)
- Delete: `.github/scripts/` directory if now empty

- [ ] **Step 5.1: Delete the firmware feature directories.**

```sh
git rm -r src/features/ticker src/features/radar
```

- [ ] **Step 5.2: Delete the Ticker automation and quote config.**

```sh
git rm -r n8n
git rm quotes-config.json
git rm -f .github/scripts/fetch-quotes.mjs 2>/dev/null || rm -f .github/scripts/fetch-quotes.mjs
rmdir .github/scripts 2>/dev/null || true
```

(`fetch-quotes.mjs` may already be unstaged if the previous workflow deletion left it; `git rm -f` handles both.)

- [ ] **Step 5.3: Confirm no remaining source references Ticker/Radar headers.**

```sh
grep -nR "TickerMode\|StockClient\|StockData\|RadarMode\|RadarClient\|RadarData\|WITH_TICKER\|WITH_RADAR" src/ || echo "clean"
```

Expected matches: `src/main.cpp` (the `#if WITH_TICKER` include guards and registry entries) and `src/WebPortal.cpp` (the `features.ticker`/`features.radar` booleans). Both are fixed in Task 7. Anywhere else is a missed reference — fix it.

- [ ] **Step 5.4: Do NOT commit yet — the build is broken until Task 7.**

---

## Task 6: Delete the Claude mascot from the usage feature

**Files:**
- Delete: `src/features/usage/Mascot.{h,cpp}`
- Delete: `src/features/usage/mascot_frames.h`
- Modify: `src/features/usage/UsageMode.{h,cpp}` — remove mascot references, keep the stats screen skeleton (Plan 2 replaces it entirely)
- Keep: `src/features/usage/UsageClient.{h,cpp}`, `UsageData.h` (Plan 2 rewrites these; for now they still compile because nothing else includes them after main.cpp is fixed in Task 7)

- [ ] **Step 6.1: Delete mascot files.**

```sh
git rm src/features/usage/Mascot.h src/features/usage/Mascot.cpp src/features/usage/mascot_frames.h
```

- [ ] **Step 6.2: Strip `UsageMode.cpp` to a non-mascot skeleton.**

Open `src/features/usage/UsageMode.cpp` (197 lines). It currently has: palette constants (cpp:10-14), `drawMeter` (cpp:59-91), `drawUsage` (cpp:94), `drawMascot` (cpp:122), `service()` (cpp:166). Replace the entire file with a minimal skeleton that keeps the `DisplayMode` interface intact but renders nothing fancy — Plan 2 rewrites it:

```cpp
// UsageMode.cpp — placeholder.
//
// 3.0.0 replaces this with the CODEX/ZAI/AUTO usage display (Plan 2). This
// skeleton keeps the DisplayMode interface alive so main.cpp still compiles
// after Ticker/Radar/mascot removal.
#include "UsageMode.h"
#include "UsageClient.h"

UsageMode g_usageMode;

void UsageMode::begin(const Settings& s)   { needRender_ = true; usageInit(s); }
void UsageMode::service(const Settings& s) { usageService(s); }
void UsageMode::invalidate(const Settings& s) { needRender_ = true; }
void UsageMode::wake(const Settings& s)    { needRender_ = true; }
```

- [ ] **Step 6.3: Strip `UsageMode.h` to match.**

Open `src/features/usage/UsageMode.h` (27 lines). Replace its body with:

```cpp
// UsageMode.h — placeholder. Plan 2 rewrites this.
#pragma once
#include "Mode.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override        { return "usage"; }
  uint8_t     modeConst() const override { return MODE_USAGE; }
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
 private:
  bool needRender_ = true;
};

extern UsageMode g_usageMode;
```

- [ ] **Step 6.4: Do NOT commit yet — Tasks 5 and 6 commit together with main.cpp/WebPortal in Task 7.**

---

## Task 7: Fix main.cpp, WebPortal.cpp, and webui.h after the deletions

**Files:**
- Modify: `src/main.cpp:22-89` — drop Ticker/Radar guards and registry entries; simplify carousel
- Modify: `src/WebPortal.cpp` — drop `features.ticker/radar`, drop `/api/refresh` route
- Modify: `src/webui.h` — drop ticker/radar tabs, nav buttons, and JS

- [ ] **Step 7.1: main.cpp — drop Ticker/Radar from the registry.**

Open `src/main.cpp`. Edit the include guards (lines 22-30) to leave only:

```cpp
#if WITH_USAGE
#include "UsageMode.h"
#endif
```

Edit the registry (lines 35-45) to leave only:

```cpp
static DisplayMode* kModes[] = {
#if WITH_USAGE
  &g_usageMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);
```

Edit `carouselHas` (lines 54-61) to leave only the usage case:

```cpp
static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
    case MODE_USAGE:  return s.carouselUsage;
    default:          return true;
  }
}
```

The carousel logic itself (lines 63-89) stays — with only one mode in the registry the carousel is effectively a no-op, but Plan 2 will repurpose `carouselSec` into `autoRotateSec` so do not delete the mechanism.

- [ ] **Step 7.2: WebPortal.cpp — drop Ticker/Radar feature flags and `/api/refresh`.**

Open `src/WebPortal.cpp`. Find the `features` object in `handleGetConfig` (around cpp:47-58) and reduce it to:

```cpp
JsonObject feats = root["features"].to<JsonObject>();
feats["usage"] = (bool)WITH_USAGE;
```

Find and delete the route registration for `/api/refresh` (calls `stocksForceRefresh()` — the Ticker client no longer exists) in the route table around cpp:291-311, and delete the `handleRefresh` handler if it is defined in this file.

If `handleStatus` references `stocksForceRefresh`, ticker data, or radar data, strip those fields too — leave only heap, RSSI, reset reason, and (if present) a usage summary object.

- [ ] **Step 7.3: webui.h — drop Ticker and Radar UI.**

Open `src/webui.h` (634 lines, single PROGMEM string). Delete:
- The two `<button class="tab-btn" data-tab="ticker">` and `data-tab="radar">` entries in the `<nav>` (around line 59).
- The `<section class="tab" id="ticker">...</section>` block (around lines 141-199).
- The `<section class="tab" id="radar">...</section>` block (around lines 210-253).
- In the `<section class="tab" id="usage">` block (around lines 200-208), delete the `usageUrl` input row (Plan 2 adds the new usage UI here; for now leave an empty usage tab so the JS does not error on a missing element).
- In the JS: find `CAROPT` map (around line 346) and remove the `ticker`/`radar` entries, leaving only `usage`. Find any `if (C.features.ticker)` / `if (C.features.radar)` branches and delete them. Find the `forceRefresh()`/`/api/refresh` button handler (if any) and delete it.

- [ ] **Step 7.4: Build all three envs.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -15
uvx --from platformio platformio run -e smalltv_loader 2>&1 | tail -8
uvx --from platformio platformio run -e clock_usb 2>&1 | tail -8
```

Expected: all three `SUCCESS`. If `clock_usb` fails, you accidentally edited `usb_clock.cpp` — revert it.

- [ ] **Step 7.5: Commit Tasks 5+6+7 together (they are one logical "delete features + rewire callers" change).**

```sh
git add -A
git commit -m "refactor(v3): delete Ticker, Radar, Claude mascot

- Remove src/features/{ticker,radar} and mascot files (70KB PROGMEM)
- Strip main.cpp registry, WebPortal features/routes, webui tabs
- Delete n8n automation, quotes-config.json, dead fetch-quotes proxy
- Skeleton UsageMode kept for Plan 2 to rewrite"
```

---

## Task 8: Collapse Settings to the post-Ticker/Radar schema

**Files:**
- Modify: `src/Settings.h` — delete `SymbolCfg`, `Airport`, `TickerSettings`, `RadarSettings`; delete `Settings.ticker/radar/carouselTicker/carouselRadar`
- Modify: `src/Settings.cpp` — delete `srcToJson/fromJson`, `TickerSettings::*`, `RadarSettings::*`, the ticker/radar blocks in `settingsToJson`/`settingsApplyJson`

**Note on `Settings.usage`:** Plan 2 redefines `UsageSettings` (drops `usageUrl`, adds `mode`/`autoRotateSec`-related fields). In THIS plan, leave `UsageSettings` as-is (`usageUrl` + `pollSec`) so the build stays green; Plan 2 task 1 will rewrite it.

- [ ] **Step 8.1: Settings.h — delete ticker/radar structs.**

Open `src/Settings.h`. Delete:
- `struct SymbolCfg` (h:14-20)
- `struct Airport` (h:22-26)
- `struct TickerSettings` (h:35-63) and its forward-declarations
- `struct RadarSettings` (h:89-111) and its forward-declarations
- The comment block "Plane radar feature slice" (h:89)

In `struct Settings` (h:114-145) delete:
- `TickerSettings ticker;` (h:139)
- `RadarSettings radar;` (h:141)
- `bool carouselTicker, carouselRadar;` from h:129 — leave `carouselUsage` and `carouselSec`

Update the top-of-file doc comment (h:1-9) to drop "ticker / usage / radar" segmentation language — it is now just "shared device/network fields + usage slice + clock slice".

- [ ] **Step 8.2: Settings.cpp — delete ticker/radar implementations.**

Open `src/Settings.cpp`. Delete:
- `srcToJson` and `srcFromStr` (cpp:10-19)
- `TickerSettings::setDefaults/toJson/fromJson` (cpp:21-119)
- `RadarSettings::setDefaults/toJson/fromJson` (cpp:185-260)
- In `Settings::setDefaults` (cpp:265-291): delete `ticker.setDefaults();` and `radar.setDefaults();` (cpp:287, 289); delete `carouselTicker = ... = carouselRadar = true;` and replace with `carouselUsage = true;`
- In `settingsToJson` (cpp:333-374): delete `root["carouselTicker"]` (cpp:360), `root["carouselRadar"]` (cpp:362), `s.ticker.toJson(...)` (cpp:370), `s.radar.toJson(...)` (cpp:372)
- In `settingsApplyJson` (cpp:378-447): delete `if (root["carouselTicker"]...)` (cpp:427), `if (root["carouselRadar"]...)` (cpp:429); delete the `JsonObjectConst t = root["ticker"]...` line and `s.ticker.fromJson(t);` (cpp:440-441); delete the radar block (cpp:444-445); in the comment at cpp:437-439 drop the ticker reference

- [ ] **Step 8.3: Build.**

```sh
uvx --from platformio platformio run -e smalltv 2>&1 | tail -15
```

Expected: `SUCCESS`. If `main.cpp` or `WebPortal.cpp` now complains about a removed `s.ticker`/`s.radar`/`carouselTicker`/`carouselRadar` reference, you missed a spot in Task 7 — fix it.

- [ ] **Step 8.4: Commit.**

```sh
git add src/Settings.h src/Settings.cpp
git commit -m "refactor(v3): drop Ticker/Radar settings slices

Keeps Settings.usage (Plan 2 redefines it). carousel{Sec,Usage} retained;
carousel{Ticker,Radar} removed."
```

---

## Task 9: Rename `smalltv`/`smalltv_loader` envs to `smalltv_ultra`/`smalltv_ultra_loader`

**Files:**
- Modify: `platformio.ini` — rename envs, set `default_envs = smalltv_ultra`, drop ESP32 envs and pioarduino URL

- [ ] **Step 9.1: Edit `platformio.ini`.**

- Change `[platformio] default_envs = smalltv` to `default_envs = smalltv_ultra`.
- Rename `[env:smalltv]` to `[env:smalltv_ultra]`. Update the file-top comment to mention `smalltv_ultra`, `smalltv_ultra_loader`, and `clock_usb` as the only three targets.
- Rename `[env:smalltv_loader]` to `[env:smalltv_ultra_loader]`.
- Delete the entire `[env:smalltv_c2]` section (platformio.ini:102-138).
- Delete the entire `[env:smalltv_esp32]` section (platformio.ini:140-173).
- Delete the `pioarduino/platform-espressif32` URL — no env references it anymore.
- In `[env:smalltv_ultra]`, the `build_src_flags` `-iquote src/features/{ticker,radar}` lines are now dead (those dirs are gone) — delete them, leaving only `-iquote src` and `-iquote src/features/usage`.

- [ ] **Step 9.2: Verify the rename with a clean build of each surviving env.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -8
uvx --from platformio platformio run -e smalltv_ultra_loader 2>&1 | tail -8
uvx --from platformio platformio run -e clock_usb 2>&1 | tail -8
```

Expected: all three `SUCCESS`. If `smalltv_ultra_loader` fails, check that `loader.cpp` still compiles after the Platform.h/OtaUpdate.h simplifications (it should — it does not include the feature layer).

- [ ] **Step 9.3: Confirm ESP32 envs are gone.**

```sh
grep -n "smalltv_c2\|smalltv_esp32\|pioarduino\|esp32-c2\|esp32dev" platformio.ini || echo "clean"
```

Expected: `clean`.

- [ ] **Step 9.4: Commit.**

```sh
git add platformio.ini
git commit -m "refactor(v3): rename envs to smalltv_ultra(_loader); drop ESP32 envs

Three surviving targets: smalltv_ultra, smalltv_ultra_loader, clock_usb."
```

---

## Task 10: Verify the spec's build gates pass

**Files:** none modified — verification only.

- [ ] **Step 10.1: Run the spec's build gates (§build gates).**

```sh
uvx --from platformio platformio run -e smalltv_ultra        2>&1 | tail -6
uvx --from platformio platformio run -e smalltv_ultra_loader 2>&1 | tail -6
uvx --from platformio platformio run -e clock_usb            2>&1 | tail -6
git diff --check
uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin 2>&1 | tail -15
```

Expected:
- All three PlatformIO runs: `[SUCCESS]`.
- `git diff --check`: no whitespace errors.
- image-info: `Target chip: ESP8266`, `Flash size: 4 MB`, `Flash mode: DIO`, `Checksum: valid`.

- [ ] **Step 10.2: Confirm footprint shrank vs baseline (spec §build gates: "smaller than ~682 KB baseline").**

```sh
ls -la .pio/build/smalltv_ultra/firmware.bin
# Compare bytes against _baseline-smalltv-image-info.txt captured in pre-flight P3.
```

Expected: image is well under 682 KB (mascot removal alone shed ~70 KB; ticker/radar remove more). Record the new size for the final summary.

- [ ] **Step 10.3: Confirm no compiler warnings from project code.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | grep -i "warning:" | grep -v "elf2bin\|SyntaxWarning\|platformio/packages" || echo "no project warnings"
```

Expected: `no project warnings`. Per AGENTS.md, ignore the Python `SyntaxWarning` messages from `elf2bin.py` (third-party tool warnings, not firmware warnings).

- [ ] **Step 10.4: Confirm ESP32 remnants are gone from source.**

```sh
grep -nR "SMALLTV_ESP32\|board_esp32\|smalltv_c2\|smalltv_esp32\|NM-TV-154\|esp32-c2\|esp32c2" src/ platformio.ini || echo "clean"
grep -nR "TickerMode\|StockClient\|RadarMode\|RadarClient\|WITH_TICKER\|WITH_RADAR\|Mascot" src/ || echo "clean"
```

Expected: both `clean`.

- [ ] **Step 10.5: Confirm clock_usb still answers (visual gate happens later, but a build+status check now catches regressions early).**

If a physical clock is connected and you have user authorization to flash, you may optionally verify `clock_usb` still behaves identically:

```sh
# Only if explicitly authorized to flash in this session:
uvx --from esptool esptool \
  --port "$(uv run --with pyserial tools/clockctl.py find-port)" --baud 115200 \
  write-flash 0x0 .pio/build/clock_usb/firmware.bin
uv run --with pyserial tools/clockctl.py status
```

Expected (per AGENTS.md flashing rule #3): the firmware answers `STATUS`. If you do NOT have authorization to flash, skip this step — Plan 4 ends with a physical acceptance gate that covers it.

- [ ] **Step 10.6: No commit (verification only).**

---

## Self-Review (run after Step 10.6, before handing off to Plan 2)

- [ ] **Coverage of spec §1 (Make repository SmallTV-ultra only):**
  - PlatformIO has only `smalltv_ultra`, `smalltv_ultra_loader`, `clock_usb`? ✓ Task 9.
  - Default env is `smalltv_ultra`? ✓ Task 9.1.
  - ESP32-C2/NM-TV-154 envs, pioarduino dep, partition table deleted? ✓ Tasks 2, 9.
  - `board_smalltv_ultra.h` included directly (no macro)? ✓ Task 1.
  - `board_esp32*.h` deleted? ✓ Task 2.
  - `Platform/Gfx/OtaUpdate/config` collapsed to ESP8266-only? ✓ Tasks 3, 4.
  - Web status/UI reports `SmallTV-ultra · ESP8266`? — **Plan 2 task (WebPortal handleStatus). Flag it.**
  - ESP32 docs/images/release artifacts removed? — **Plan 4 owns docs. Flag it.**

- [ ] **Coverage of spec §2 (Delete Ticker, Radar and Claude UI):**
  - Feature registry/clients/renderers deleted? ✓ Tasks 5, 6.
  - `WITH_TICKER`/`WITH_RADAR`/`WITH_USAGE`/include paths/carousel flags gone? ✓ Tasks 4, 5, 7, 8.
  - Quote-fetch workflow/script + Ticker/Radar docs deleted? ✓ Task 5 (workflow already deleted upstream; script deleted here). Docs → Plan 4.
  - Claude mascot, frames, extraction tool, palette/text, `_clawdmeter._tcp` deleted? Mascot/frames ✓ Task 6. `_clawdmeter` mDNS → Plan 2 renames it. `extract_mascot.py` — **still present (per spec: do not modify tools). Flag it for Plan 4.**
  - `usageUrl` and pull mode removed from firmware? — **Plan 2 (the placeholder UsageMode still calls `usageInit`/`usageService` which read `usageUrl`; Plan 2 rewrites). Flag it.**
  - `usb_clock.cpp`, USB gallery/reminder/Face protocol, legacy USB collector untouched? ✓ No task modifies them.
  - `codex_usage.py` CLI output preserved? ✓ No task modifies tools.

- [ ] **Placeholder scan:** every step above has concrete commands or code. The two `// Flag it` items above are deliberate cross-plan hand-offs, not plan placeholders.

- [ ] **Type consistency:** `MODE_USAGE` is preserved in config.h so `UsageMode::modeConst()` still compiles; `WITH_USAGE` is preserved so Net.cpp's mDNS guard still compiles; `Settings.usage` is preserved so Settings.cpp still compiles. Plan 2 will redefine the mode enum and `UsageSettings`, but the names it needs to keep alive are kept alive.

---

## Done criteria for Plan 1

1. All three PlatformIO envs (`smalltv_ultra`, `smalltv_ultra_loader`, `clock_usb`) build with no project-code compiler warnings.
2. `image-info` on `smalltv_ultra/firmware.bin` reports ESP8266, 4 MB, DIO, valid checksum, and a size well under 682 KB.
3. `git diff --check` is clean.
4. No source file references ESP32, C2, NM-TV-154, Ticker, Radar, Mascot, or `WITH_TICKER`/`WITH_RADAR`.
5. `usb_clock.cpp`, `tools/`, `tests/`, and `loader.cpp` are byte-for-byte unchanged (modulo the env rename in platformio.ini).
6. The working tree is committed (no stray changes) and the branch is ready to receive Plan 2.

**Hand-off to Plan 2:** `docs/superpowers/plans/2026-07-21-v3-plan2-usage-firmware.md`.
