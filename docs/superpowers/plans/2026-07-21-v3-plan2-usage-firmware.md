# SWC Digital 3.0.0 — Plan 2: Usage Display Firmware (CODEX / Z.AI / AUTO)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder usage feature (left by Plan 1) with a real two-provider usage display. The device exposes `POST /api/usage` (Mac pushes Codex and z.ai data over Wi-Fi), `GET /api/usage` (read snapshot), advertises `_aiusage._tcp` on mDNS, and renders three modes: `CODEX`, `Z.AI`, and `AUTO` (alternates Codex ↔ ZAI every 30 s). Data older than 180 s is dimmed and marked `STALE`.

**Architecture:** One central `UsageStore` owns both providers' snapshots (with validation, freshness, countdown). One `UsageMode` renderer takes a provider id + theme and draws the same layout. `AUTO` is a mode that toggles the active provider on a timer. The push API validates strictly (per-provider independent; one bad request never touches the last-good snapshot). Partial dirty-region redraws only: full redraw on mode/provider/rotation change, status+cards on new data, countdown/age only when their minute value changes. Never clear the full panel in the main loop.

**Tech Stack:** PlatformIO `espressif8266`, ArduinoJson 7, GFX Library for Arduino, `ESP8266WebServer`, `ESP8266WiFi`/`ESP8266mDNS`, LittleFS.

**Branch:** `feature/v3-usage-display` (Plan 1 leaves the branch in a clean, building state).

**Depends on:** Plan 1 complete (Ticker/Radar/mascot deleted, `Settings` slice simplified, env renamed to `smalltv_ultra`).

**Spec source:** `pasted-text-20260721-155906-89253a9e.txt` §3 (Usage domain and display modes), §4 (Settings migration), and the Public Interfaces section (Push/Read API, mDNS, Web UI).

---

## Pre-flight

- [ ] **P1: Confirm Plan 1 done-criteria hold.**

```sh
git log --oneline -15          # Plan 1 commits present
git branch --show-current      # feature/v3-usage-display
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -4   # [SUCCESS]
grep -nR "TickerMode\|RadarMode\|Mascot\|SMALLTV_ESP32" src/ || echo "clean"
```

Expected: Plan 1 commits on top, `[SUCCESS]`, and `clean`. If any check fails, finish Plan 1 first.

---

## File Structure (what changes in this plan)

**Created:**
- `src/features/usage/UsageStore.h` / `.cpp` — owns both providers' snapshots, validation, freshness, countdown. Replaces the old `UsageData.h` role.
- `src/features/usage/UsageApi.h` / `.cpp` — HTTP push/read handlers for `/api/usage` (called from WebPortal).

**Rewritten:**
- `src/features/usage/UsageMode.h` / `.cpp` — the renderer (Plan 1 left a stub).
- `src/features/usage/UsageClient.h` / `.cpp` — deleted (no pull mode in v3). Its old push entry point `usageApply()` is replaced by `UsageStore::applyPush()`.
- `src/features/usage/UsageData.h` — deleted (replaced by `UsageStore`).

**Modified:**
- `src/Settings.h` / `Settings.cpp` — replace `UsageSettings` (drops `usageUrl`/`pollSec`, adds `mode`/`autoRotateSec`); add `schemaVersion` migration in `loadSettings`.
- `src/config.h` — replace the mode enum with `MODE_CODEX/MODE_ZAI/MODE_AUTO`; add usage palette constants and `USAGE_STALE_AFTER_MS`.
- `src/main.cpp` — call `usageApiBegin()` from setup; replace `carouselSec` timer logic with the AUTO rotate logic (provider toggle, not mode toggle).
- `src/Net.cpp` — rename `_clawdmeter._tcp` → `_aiusage._tcp` with new TXT records.
- `src/WebPortal.cpp` — wire `/api/usage` GET and POST to the new `UsageApi`; report `SmallTV-ultra · ESP8266` in `handleStatus`; add `/api/usage` GET route.
- `src/webui.h` — replace the Usage tab with Mode selector + autoRotateSec + both providers' 5H/Weekly/age/LIVE-STALE; wire a refresh button that only reads state (no provider API call).

**Untouched:** `usb_clock.cpp`, `tools/`, `tests/`, `loader.cpp`, `Clock.{h,cpp}`, `Platform.{h,cpp}`, `Gfx.{h,cpp}` (we use its primitives), `OtaUpdate.{h,cpp}`, `BearSslTuning.cpp`.

---

## Task 1: Define the new UsageData types and the mode enum

**Files:**
- Create: `src/features/usage/UsageStore.h` (data types only in this task; impl in Task 2)
- Modify: `src/config.h` — replace mode enum, add usage palette/timing constants
- Delete: `src/features/usage/UsageData.h`

- [ ] **Step 1.1: Replace the mode enum in `src/config.h`.**

Find the modes block (Plan 1 left `MODE_USAGE`/`MODE_CAROUSEL`/`DEFAULT_MODE`). Replace with:

```cpp
// ---- Display modes (3.0.0) ------------------------------------------------
// Three UI modes for the usage display. AUTO rotates between CODEX and ZAI.
enum DisplayMode : uint8_t {
  MODE_CODEX = 0,
  MODE_ZAI   = 1,
  MODE_AUTO  = 2,
};
#define DEFAULT_MODE  MODE_AUTO
```

Delete `MODE_CAROUSEL` entirely (Plan 1 already deleted `MODE_STOCKS`/`MODE_RADAR`). Search for any remaining `MODE_CAROUSEL` references and fix them — they will be in `Settings.cpp` (mode token mapping) and `main.cpp` (carousel block) which later tasks rewrite.

- [ ] **Step 1.2: Add usage palette and timing constants to `src/config.h`.**

Append (replacing the old `USAGE_STALE_GRACE_MS` constant):

```cpp
// ---- Usage display (3.0.0) ------------------------------------------------
#define USAGE_STALE_AFTER_MS    180000UL   // mark STALE after 180 s without a push
#define USAGE_AUTOROTATE_SEC    30         // AUTO: dwell on each provider
#define USAGE_AUTOROTATE_MIN    5
#define USAGE_AUTOROTATE_MAX    3600

// Palette (RGB565). Match the spec exactly.
#define USAGE_COLOR_CODEX       0x29B0     // #10A37F
#define USAGE_COLOR_ZAI         0x6273     // #6C63FF (approx; verify below)
#define USAGE_COLOR_BG          0x0410     // #081018
#define USAGE_COLOR_CARD        0x1A82     // #111C26
#define USAGE_COLOR_TEXT        0xFFFF     // #F4F7FA
#define USAGE_COLOR_MUTED       0xAD55     // #8EA1B2
#define USAGE_COLOR_WARN        0xFD87     // #FFB020
#define USAGE_COLOR_CRIT        0xB869     // #F05252
#define USAGE_COLOR_STALE       0x738C     // #677786
```

**Note on the RGB565 values:** the values above are approximations. The implementer MUST compute the exact RGB565 for each `#RRGGBB` in the spec using the standard formula `(R & 0xF8) << 8 | (G & 0xFC) << 3 | (B >> 3)` and put those exact constants in. Document the computation in a comment next to each define:

```cpp
// #RRGGBB → RGB565: ((R&0xF8)<<8)|((G&0xFC)<<3)|(B>>3)
#define USAGE_COLOR_CODEX   0x1526   // #10A37F: (0x10&0xF8)<<8|(0x37&0xFC)<<3|0x7F>>3 ...
```

(Recompute each one precisely. Do not ship the approximations.)

- [ ] **Step 1.3: Create `src/features/usage/UsageStore.h` with the data types.**

```cpp
// UsageStore.h — snapshots + validation + freshness for two providers.
//
// One instance (g_usageStore) is the single source of truth. The HTTP push API
// writes here; UsageMode reads here. Pull mode was removed in 3.0.0 — the Mac
// service is the only data source.
#pragma once
#include <Arduino.h>

enum UsageProvider : uint8_t {
  PROVIDER_CODEX = 0,
  PROVIDER_ZAI   = 1,
  PROVIDER_COUNT = 2,
};

struct UsageWindow {
  bool     available;     // false => render "N/A"
  uint8_t  usedPct;       // 0..100 when available
  uint16_t resetMin;      // minutes until the window resets (0xFFFF when unknown)
};

struct ProviderUsage {
  UsageWindow fiveHour;
  UsageWindow weekly;
  bool        everReceived;   // has any valid push landed for this provider?
  uint32_t    lastOkMs;       // millis() of the last accepted push
};

class UsageStore {
 public:
  void begin();
  // Apply one provider's push. `json` is the raw POST body. Returns true if the
  // body validated AND at least one window landed (per-provider last-good is
  // untouched on validation failure). On success updates lastOkMs.
  bool applyPush(UsageProvider p, const String& json);
  // Snapshot read for rendering. `providerTheme` returns the spec palette color.
  const ProviderUsage& read(UsageProvider p) const;
  uint32_t ageMs(UsageProvider p) const;        // millis() since lastOkMs; 0xFFFFFFFF if never
  bool    stale(UsageProvider p) const;         // ageMs() > USAGE_STALE_AFTER_MS
  // Format the snapshot as the GET /api/usage JSON (both providers + age + stale).
  void    serializeOverview(String& out) const;
 private:
  ProviderUsage data_[PROVIDER_COUNT];
};

extern UsageStore g_usageStore;

// Theme per provider (color used for the title and the under-threshold bar fill).
uint16_t usageProviderColor(UsageProvider p);   // USAGE_COLOR_CODEX or USAGE_COLOR_ZAI
const char* usageProviderLabel(UsageProvider p); // "CODEX" or "Z.AI"
```

- [ ] **Step 1.4: Delete the old `src/features/usage/UsageData.h`.**

```sh
git rm src/features/usage/UsageData.h
```

- [ ] **Step 1.5: Build (will fail — UsageClient and the stub UsageMode still reference deleted names).**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -15
```

Expected: errors in `UsageClient.cpp` and `UsageMode.cpp` (deleted `UsageData.h`, undefined `g_usageStore`). Tasks 2 and 3 fix these. Proceed.

- [ ] **Step 1.6: Commit (build intentionally broken until Task 3).**

```sh
git add src/config.h src/features/usage/UsageStore.h
git rm src/features/usage/UsageData.h 2>/dev/null || true
git commit -m "feat(v3): define UsageStore types and CODEX/ZAI/AUTO mode enum"
```

---

## Task 2: Implement `UsageStore` (validation + freshness + serialization)

**Files:**
- Create: `src/features/usage/UsageStore.cpp`

**Reference — the push body contract (spec §Push API):**

```json
{
  "v": 1,
  "provider": "codex",                  // or "zai"
  "five_hour_used_pct": null,           // integer 0..100 or null
  "five_hour_reset_min": null,          // integer 0..65535 or null
  "weekly_used_pct": 91,
  "weekly_reset_min": 5890
}
```

Validation rules (spec §Push API rules):
- `v` MUST be `1`.
- `provider` MUST be `"codex"` or `"zai"` (the route also tells us the provider; we trust the body's `provider` only when the caller does not pass it implicitly — in our design the POST body's `provider` is authoritative because the Mac service pushes per-provider).
- `percentage` is integer `0..100` or `null`. `reset` is integer `0..65535` or `null`.
- At least one window MUST have a percentage (else 400, no state change).
- 400 must not change the last-good snapshot.

- [ ] **Step 2.1: Create `src/features/usage/UsageStore.cpp`.**

```cpp
#include "UsageStore.h"
#include "../../config.h"
#include <ArduinoJson.h>

UsageStore g_usageStore;

void UsageStore::begin() {
  for (uint8_t i = 0; i < PROVIDER_COUNT; i++) {
    data_[i].fiveHour.available = false;
    data_[i].fiveHour.usedPct    = 0;
    data_[i].fiveHour.resetMin   = 0xFFFF;
    data_[i].weekly.available    = false;
    data_[i].weekly.usedPct      = 0;
    data_[i].weekly.resetMin     = 0xFFFF;
    data_[i].everReceived        = false;
    data_[i].lastOkMs            = 0;
  }
}

// Parse one optional window object: {"used_pct": int|null, "reset_min": int|null}
// Returns false if the values are present but out of range (caller rejects the push).
static bool parseWindow(JsonObjectConst o, const char* pctKey, const char* resetKey,
                        UsageWindow& out, bool& sawPct) {
  JsonVariantConst pct = o[pctKey];
  if (!pct.isNull()) {
    if (!pct.is<int>()) return false;
    int v = pct.as<int>();
    if (v < 0 || v > 100) return false;
    out.usedPct   = (uint8_t)v;
    out.available = true;
    sawPct        = true;
  }
  JsonVariantConst reset = o[resetKey];
  if (!reset.isNull()) {
    if (!reset.is<int>()) return false;
    int v = reset.as<int>();
    if (v < 0 || v > 65535) return false;
    out.resetMin = (uint16_t)v;
  } else {
    out.resetMin = 0xFFFF;
  }
  return true;
}

bool UsageStore::applyPush(UsageProvider p, const String& json) {
  if (p >= PROVIDER_COUNT) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();

  // Strict validation BEFORE touching state.
  if (root["v"].is<int>()) {
    if (root["v"].as<int>() != 1) return false;
  } else {
    return false;   // v is required and must be the integer 1
  }

  // Validate provider token matches the route's provider.
  if (root["provider"].is<const char*>()) {
    const char* tok = root["provider"];
    const char* want = (p == PROVIDER_CODEX) ? "codex" : "zai";
    if (strcasecmp(tok, want) != 0) return false;
  } else {
    return false;   // provider is required
  }

  ProviderUsage next = data_[p];    // work on a copy; commit only on success
  // Reset window availability for this push (a window missing in the body = N/A).
  next.fiveHour.available = false;
  next.weekly.available   = false;
  next.fiveHour.resetMin  = 0xFFFF;
  next.weekly.resetMin    = 0xFFFF;

  bool sawPct = false;
  if (!parseWindow(root, "five_hour_used_pct", "five_hour_reset_min",
                   next.fiveHour, sawPct)) return false;
  if (!parseWindow(root, "weekly_used_pct", "weekly_reset_min",
                   next.weekly, sawPct)) return false;
  if (!sawPct) return false;   // must have at least one window with a percentage

  // Commit.
  data_[p]         = next;
  data_[p].everReceived = true;
  data_[p].lastOkMs     = millis();
  return true;
}

const ProviderUsage& UsageStore::read(UsageProvider p) const {
  return data_[p];
}

uint32_t UsageStore::ageMs(UsageProvider p) const {
  if (!data_[p].everReceived) return 0xFFFFFFFFUL;
  return millis() - data_[p].lastOkMs;
}

bool UsageStore::stale(UsageProvider p) const {
  return ageMs(p) > USAGE_STALE_AFTER_MS;
}

void UsageStore::serializeOverview(String& out) const {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["schema"] = 1;
  static const char* NAMES[PROVIDER_COUNT] = { "codex", "zai" };
  JsonArray arr = root["providers"].to<JsonArray>();
  for (uint8_t i = 0; i < PROVIDER_COUNT; i++) {
    JsonObject po = arr.add<JsonObject>();
    po["provider"] = NAMES[i];
    po["everReceived"] = data_[i].everReceived;
    uint32_t age = ageMs((UsageProvider)i);
    po["age_sec"] = (age == 0xFFFFFFFFUL) ? -1 : (int32_t)(age / 1000UL);
    po["stale"]   = stale((UsageProvider)i);
    JsonObject five = po["five_hour"].to<JsonObject>();
    if (data_[i].fiveHour.available) {
      five["used_pct"]   = data_[i].fiveHour.usedPct;
      five["reset_min"]  = (data_[i].fiveHour.resetMin == 0xFFFF)
                             ? -1 : (int32_t)data_[i].fiveHour.resetMin;
    }
    JsonObject wk = po["weekly"].to<JsonObject>();
    if (data_[i].weekly.available) {
      wk["used_pct"]   = data_[i].weekly.usedPct;
      wk["reset_min"]  = (data_[i].weekly.resetMin == 0xFFFF)
                           ? -1 : (int32_t)data_[i].weekly.resetMin;
    }
  }
  serializeJson(doc, out);
}

uint16_t usageProviderColor(UsageProvider p) {
  return (p == PROVIDER_CODEX) ? USAGE_COLOR_CODEX : USAGE_COLOR_ZAI;
}
const char* usageProviderLabel(UsageProvider p) {
  return (p == PROVIDER_CODEX) ? "CODEX" : "Z.AI";
}
```

- [ ] **Step 2.2: Build (still fails — UsageMode references the old API).**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -10
```

Expected: errors only in `UsageMode.{h,cpp}` and `UsageClient.{h,cpp}`. Task 3 fixes them.

- [ ] **Step 2.3: Commit.**

```sh
git add src/features/usage/UsageStore.cpp
git commit -m "feat(v3): implement UsageStore (strict push validation + freshness)"
```

---

## Task 3: Rewrite `UsageMode` as the renderer; delete `UsageClient`

**Files:**
- Rewrite: `src/features/usage/UsageMode.h`
- Rewrite: `src/features/usage/UsageMode.cpp`
- Delete: `src/features/usage/UsageClient.h` / `UsageClient.cpp`

- [ ] **Step 3.1: Delete `UsageClient`.**

```sh
git rm src/features/usage/UsageClient.h src/features/usage/UsageClient.cpp
```

- [ ] **Step 3.2: Rewrite `src/features/usage/UsageMode.h`.**

```cpp
// UsageMode.h — the usage display renderer (CODEX / Z.AI / AUTO).
//
// One renderer, parameterised by provider. In AUTO the active provider is
// toggled by main.cpp on the autoRotateSec timer. Redraw rules:
//   - full redraw: on mode change, provider change, rotation change, visual cfg
//   - status+cards: on new data (lastOkMs changed)
//   - countdown + age: only when their minute value changes
// Never clear/redraw the full panel every second.
#pragma once
#include "Mode.h"
#include "UsageStore.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override        { return "usage"; }
  uint8_t     modeConst() const override { return MODE_AUTO; }  // see note below
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

  // Called by main.cpp when AUTO rotates: repaint the new provider.
  void setActiveProvider(UsageProvider p);
 private:
  bool needsFullRedraw_ = true;
  UsageProvider active_ = PROVIDER_CODEX;
  // Last values rendered (dirty tracking).
  uint32_t lastFiveHourOk_[PROVIDER_COUNT]  = {0};
  uint32_t lastWeeklyOk_[PROVIDER_COUNT]    = {0};
  uint16_t lastFiveHourReset_[PROVIDER_COUNT] = {0xFFFF, 0xFFFF};
  uint16_t lastWeeklyReset_[PROVIDER_COUNT]   = {0xFFFF, 0xFFFF};
  uint16_t lastAgeMin_[PROVIDER_COUNT]        = {0xFFFF, 0xFFFF};
  bool     lastStale_[PROVIDER_COUNT]         = {false, false};
};

extern UsageMode g_usageMode;
```

**Note on `modeConst()`:** the `Settings.mode` field in v3 stores `DisplayMode` (CODEX/ZAI/AUTO) directly — there is no separate "usage" mode anymore. The mode IS the usage screen. So `UsageMode::modeConst()` returns `MODE_AUTO` only as a registry sentinel so `kModes[]` lookup works; the actual active provider comes from `active_` (set by main.cpp based on `Settings.mode` and the AUTO timer). Plan 1's `kModes[]` has exactly one entry (`&g_usageMode`) so the registry match is trivial.

- [ ] **Step 3.3: Rewrite `src/features/usage/UsageMode.cpp`.**

Layout per spec (240×240):
- `y=0..35`   title `CODEX`/`Z.AI` + `LIVE`/`STALE`
- `y=42..116` `5H` card
- `y=124..198` `WEEKLY` card
- `y=204..239` data age + `AUTO`/`MANUAL`

Threshold colors: `0–69` provider color, `70–89` warn, `90–100` crit. STALE overrides to muted/dim.

```cpp
#include "UsageMode.h"
#include "UsageStore.h"
#include "../../config.h"
#include "../../Gfx.h"

UsageMode g_usageMode;

static uint16_t barColorFor(uint8_t pct, uint16_t providerColor, bool stale) {
  if (stale) return USAGE_COLOR_STALE;
  if (pct >= 90) return USAGE_COLOR_CRIT;
  if (pct >= 70) return USAGE_COLOR_WARN;
  return providerColor;
}

static void drawCard(int16_t y, const char* label, const UsageWindow& w,
                     uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  // Card background.
  d->fillRoundRect(8, y, 224, 74, 6, USAGE_COLOR_CARD);
  // Label (top-left of card).
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(18, y + 8);
  d->print(label);
  // Big percentage (right side) or N/A.
  d->setTextSize(4);
  if (w.available) {
    d->setTextColor(barColorFor(w.usedPct, providerColor, stale));
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", w.usedPct);
    int16_t tw = d->textWidth(buf);  // Arduino_GFX method
    d->setCursor(222 - tw, y + 10);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "N/A";
    int16_t tw = d->textWidth(na);
    d->setCursor(222 - tw, y + 10);
    d->print(na);
  }
  // Progress bar (bottom of card).
  const int16_t by = y + 50, bh = 10, bx = 18, bw = 204;
  d->fillRoundRect(bx, by, bw, bh, 4, USAGE_COLOR_BG);
  if (w.available && w.usedPct > 0) {
    int16_t fw = (int16_t)(bw * (uint32_t)w.usedPct / 100UL);
    if (fw < 4) fw = 4;
    d->fillRoundRect(bx, by, fw, bh, 4, barColorFor(w.usedPct, providerColor, stale));
  }
  // Reset countdown (small, under the bar).
  d->setTextSize(1);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(18, by + 14);
  if (w.available && w.resetMin != 0xFFFF) {
    char buf[24];
    snprintf(buf, sizeof(buf), "RESET %um", w.resetMin);
    d->print(buf);
  } else {
    d->print("RESET --");
  }
}

void UsageMode::begin(const Settings& s) {
  needsFullRedraw_ = true;
  // Default the active provider from settings.mode: CODEX/ZAI pick directly,
  // AUTO starts on CODEX.
  active_ = (s.mode == MODE_ZAI) ? PROVIDER_ZAI : PROVIDER_CODEX;
}

void UsageMode::invalidate(const Settings& s) {
  begin(s);
}

void UsageMode::wake(const Settings& s) {
  // Re-entering from another screen: just repaint, do not refetch (there is no fetch).
  needsFullRedraw_ = true;
}

void UsageMode::setActiveProvider(UsageProvider p) {
  if (p == active_) return;
  active_ = p;
  needsFullRedraw_ = true;
}

void UsageMode::service(const Settings& s) {
  const ProviderUsage& pu = g_usageStore.read(active_);
  bool stale = g_usageStore.stale(active_);
  uint16_t providerColor = usageProviderColor(active_);

  // Full redraw path.
  if (needsFullRedraw_) {
    needsFullRedraw_ = false;
    auto* d = gfxDev();
    d->fillScreen(USAGE_COLOR_BG);
    // Title bar.
    d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
    d->setTextColor(providerColor);
    d->setTextSize(3);
    d->setCursor(10, 8);
    d->print(usageProviderLabel(active_));
    // LIVE / STALE pill (right).
    d->setTextSize(2);
    const char* pill = stale ? "STALE" : "LIVE";
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    int16_t tw = d->textWidth(pill);
    d->setCursor(232 - tw, 10);
    d->print(pill);
    // Cards.
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    // Reset dirty trackers so subsequent partial updates redraw correctly.
    for (uint8_t i = 0; i < PROVIDER_COUNT; i++) {
      lastFiveHourOk_[i]  = g_usageStore.read((UsageProvider)i).fiveHour.available
                            ? g_usageStore.read((UsageProvider)i).lastOkMs : 0;
      lastWeeklyOk_[i]    = g_usageStore.read((UsageProvider)i).weekly.available
                            ? g_usageStore.read((UsageProvider)i).lastOkMs : 0;
    }
    lastFiveHourReset_[active_] = pu.fiveHour.resetMin;
    lastWeeklyReset_[active_]   = pu.weekly.resetMin;
    lastAgeMin_[active_]        = 0xFFFF;
    lastStale_[active_]         = stale;
    return;
  }

  // Partial: status + cards only if lastOkMs changed or stale flipped.
  bool dataChanged = (pu.lastOkMs != lastFiveHourOk_[active_]) || (stale != lastStale_[active_]);
  if (dataChanged) {
    auto* d = gfxDev();
    // Repaint just the pill + cards region (y=8..198) over a fresh card bg.
    // (Cheaper than a full clear; never touches y=204..239 to avoid age flicker.)
    d->fillRect(0, 8, 240, 27, USAGE_COLOR_CARD);   // title bar minus label area
    // Re-draw label so the cleared bar is not blank.
    d->setTextColor(providerColor);
    d->setTextSize(3);
    d->setCursor(10, 8);
    d->print(usageProviderLabel(active_));
    d->setTextSize(2);
    const char* pill = stale ? "STALE" : "LIVE";
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    int16_t tw = d->textWidth(pill);
    d->setCursor(232 - tw, 10);
    d->print(pill);
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    lastFiveHourOk_[active_] = pu.lastOkMs;
    lastWeeklyOk_[active_]   = pu.lastOkMs;
    lastStale_[active_]      = stale;
  }

  // Partial: reset countdown only when the minute value changed.
  // (Codex/z.ai push reset_min as minutes already; we redraw the card's reset
  // row when it changed since last paint. Cheap: just compare to last value.)
  if (pu.fiveHour.resetMin != lastFiveHourReset_[active_] ||
      pu.weekly.resetMin   != lastWeeklyReset_[active_]) {
    // Redrawing the whole card is simplest given the small text region overlap;
    // it is bounded (74px tall) and only runs on a real change.
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    lastFiveHourReset_[active_] = pu.fiveHour.resetMin;
    lastWeeklyReset_[active_]   = pu.weekly.resetMin;
  }

  // Partial: age row (y=204..239) only when the minute value changed.
  uint32_t age = g_usageStore.ageMs(active_);
  uint16_t ageMin = (age == 0xFFFFFFFFUL) ? 0xFFFF : (uint16_t)(age / 60000UL);
  if (ageMin != lastAgeMin_[active_]) {
    auto* d = gfxDev();
    d->fillRect(0, 204, 240, 36, USAGE_COLOR_BG);
    d->setTextSize(2);
    d->setTextColor(USAGE_COLOR_MUTED);
    d->setCursor(10, 212);
    if (age == 0xFFFFFFFFUL) {
      d->print("AGE --");
    } else {
      char buf[20];
      snprintf(buf, sizeof(buf), "AGE %um", ageMin);
      d->print(buf);
    }
    // AUTO / MANUAL marker (right).
    const char* am = (s.mode == MODE_AUTO) ? "AUTO" : "MANUAL";
    d->setTextColor((s.mode == MODE_AUTO) ? providerColor : USAGE_COLOR_MUTED);
    int16_t tw = d->textWidth(am);
    d->setCursor(232 - tw, 212);
    d->print(am);
    lastAgeMin_[active_] = ageMin;
  }
}
```

**Note on `gfxDev()->textWidth()`:** Arduino_GFX's `GraphicsClass` has `int16_t textWidth(const char*)` and `int16_t textWidth(const String&)`. If the compiler complains it is not visible, the method is public on the type returned by `gfxDev()` — check `Gfx.h` for the exact alias. If `textWidth` is genuinely unavailable in the pinned GFX version, fall back to measuring with `gfxTextW()` (already in `Gfx.h` at line 96, which wraps the built-in 6×8 scaled font).

- [ ] **Step 3.4: Build.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -15
```

Expected: errors only in `main.cpp` (calls old `usageInit`/`usageService`), `WebPortal.cpp` (calls old `usageApply`), and `Settings.cpp` (old `usage.usageUrl`). Tasks 4, 5, 6 fix them.

- [ ] **Step 3.5: Commit.**

```sh
git add -A src/features/usage/
git commit -m "feat(v3): rewrite UsageMode as the CODEX/ZAI/AUTO renderer

Deletes UsageClient (pull mode removed). One renderer, parameterised by
provider. Partial dirty-region redraws only."
```

---

## Task 4: Settings — redefine `UsageSettings`, add `schemaVersion`, migrate legacy config

**Files:**
- Modify: `src/Settings.h` — replace `UsageSettings`; add `schemaVersion` to `Settings`
- Modify: `src/Settings.cpp` — migration in `loadSettings`; new `UsageSettings` methods

**Reference — new schema (spec §4):**

```json
{
  "schemaVersion": 3,
  "mode": "auto",            // "codex" | "zai" | "auto"
  "autoRotateSec": 30
}
```

Plus (kept from before): `hostname`, `wifi[]`, `apSsid/apPass`, `httpTimeout`, `brightness`, `autoBrightness`, `backlightInverted`, `rotation`, `clock{}`.

Migration rules (spec §4):
- Keep WiFi/AP/hostname/brightness/auto-brightness/backlight inversion/rotation/clock/HTTP timeout.
- Map any old mode value (`stocks`/`usage`/`radar`/`carousel`) → `auto`.
- If old `carouselSec` exists, use it as `autoRotateSec`.
- Drop ticker/radar/usage URL/carousel flags.
- Write the new schema exactly once after a successful migration.
- Output JSON after migration must NOT contain deleted keys.

- [ ] **Step 4.1: `src/Settings.h` — replace `UsageSettings` and add `schemaVersion`.**

Replace the `struct UsageSettings` block (currently has `usageUrl`/`pollSec`):

```cpp
// ---- Usage display slice (3.0.0) ------------------------------------------
struct UsageSettings {
  uint8_t  mode;            // DisplayMode: MODE_CODEX / MODE_ZAI / MODE_AUTO
  uint16_t autoRotateSec;   // 5..3600 (AUTO provider dwell)

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};
```

In `struct Settings`, add at the very top (above the WiFi fields):

```cpp
  uint16_t schemaVersion;   // 3 after migration; 0 on a fresh chip
```

- [ ] **Step 4.2: `src/Settings.cpp` — new `UsageSettings` methods.**

Replace the old `UsageSettings::setDefaults/toJson/fromJson` block:

```cpp
void UsageSettings::setDefaults() {
  mode          = DEFAULT_MODE;
  autoRotateSec = USAGE_AUTOROTATE_SEC;
}

void UsageSettings::toJson(JsonObject o) const {
  o["mode"]          = (mode == MODE_ZAI)   ? "zai"
                     : (mode == MODE_CODEX) ? "codex" : "auto";
  o["autoRotateSec"] = autoRotateSec;
}

void UsageSettings::fromJson(JsonObjectConst o) {
  if (o["mode"].is<const char*>()) {
    String m = o["mode"].as<String>();
    mode = m.equalsIgnoreCase("zai")   ? MODE_ZAI
         : m.equalsIgnoreCase("codex") ? MODE_CODEX : MODE_AUTO;
  }
  if (o["autoRotateSec"].is<int>())
    autoRotateSec = (uint16_t)constrain((int)o["autoRotateSec"],
                                        USAGE_AUTOROTATE_MIN, USAGE_AUTOROTATE_MAX);
}
```

- [ ] **Step 4.3: `Settings::setDefaults` — initialize `schemaVersion` and use the new usage defaults.**

In `Settings::setDefaults` (cpp:265-291), add at the top:

```cpp
  schemaVersion = 3;
```

and change `mode = DEFAULT_MODE;` (the top-level one — the Settings struct still has the top-level `mode` field; that field is now redundant with `usage.mode` but we keep it for the registry lookup in main.cpp). Leave `mode = DEFAULT_MODE;` as is. The authoritative source of truth for the display mode is `usage.mode`; the top-level `mode` mirrors it for the registry. (See Task 5 for how main.cpp uses `usage.mode`.)

Delete `carouselTicker`/`carouselRadar` references if any survived Plan 1. Leave `carouselSec` (it is reused as the source for `autoRotateSec` migration below). Leave `carouselUsage` (unused but harmless) OR delete it — **delete it** for cleanliness, and update `settingsToJson`/`settingsApplyJson` to drop it.

- [ ] **Step 4.4: `settingsToJson` — emit the new schema, drop deleted keys.**

Rewrite `settingsToJson` (cpp:333-374) so the output is exactly (in any order):

```json
{
  "schemaVersion": 3,
  "hostname": "...",
  "wifi": [ {"ssid":..., "passSet":..., "pass":...}, ... ],
  "apSsid": "...", "apPassSet": ..., "apPass": "...",
  "httpTimeout": 8000,
  "brightness": 70, "autoBrightness": false,
  "backlightInverted": true, "rotation": 0,
  "usage": { "mode": "auto", "autoRotateSec": 30 },
  "clock": { ... }
}
```

Key deletions vs the old output: `staSsid`/`staPass`/`staPassSet` (legacy mirror), `carouselSec`, `carouselTicker/Usage/Radar`, top-level `mode` token (now lives in `usage.mode`), and the entire `ticker`/`radar` objects. Add `root["schemaVersion"] = s.schemaVersion;`. Replace the `s.usage.toJson(...)` line (the new slice emits `mode` + `autoRotateSec`, not `usageUrl`).

**Important:** keep the WiFi `pass` masking behaviour (`includeSecrets=false` → no `pass` key). That path is unchanged.

- [ ] **Step 4.5: `settingsApplyJson` — read the new schema (lenient: missing keys are OK).**

Rewrite the body (cpp:378-447) to read: `schemaVersion` (optional), `hostname`, `wifi[]`/legacy `staSsid` (keep the legacy-single-network path), `apSsid`/`apPass`, `httpTimeout`, `brightness`, `autoBrightness`, `backlightInverted`, `rotation`, `usage{}` (the new slice), `clock{}`. Delete all ticker/radar/carousel handling.

- [ ] **Step 4.6: `loadSettings` — add the migration pass.**

Replace `loadSettings` (cpp:301-313) with a version that migrates a pre-v3 file once:

```cpp
bool loadSettings(Settings& s) {
  s.setDefaults();
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    // Fresh chip: persist the v3 defaults so later reads are consistent.
    saveSettings(s);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonObjectConst root = doc.as<JsonObjectConst>();

  // Migration: if the file predates schemaVersion 3, lift forward what we keep
  // (WiFi/AP/hostname/display/clock/httpTimeout), map any old mode to AUTO,
  // and seed autoRotateSec from the legacy carouselSec if present.
  uint16_t fileVer = root["schemaVersion"].is<int>() ? (uint16_t)root["schemaVersion"].as<int>() : 0;
  if (fileVer < 3) {
    // Apply the legacy file in place (settingsApplyJson handles both layouts
    // and the legacy mode tokens), then normalize the v3 fields.
    settingsApplyJson(s, root);
    // Force any legacy mode token to AUTO and lift carouselSec -> autoRotateSec.
    s.usage.mode = MODE_AUTO;
    if (root["carouselSec"].is<int>()) {
      int cs = root["carouselSec"].as<int>();
      s.usage.autoRotateSec = (uint16_t)constrain(cs, USAGE_AUTOROTATE_MIN, USAGE_AUTOROTATE_MAX);
    }
    s.schemaVersion = 3;
    // Persist exactly once. Subsequent saves write the clean v3 schema.
    saveSettings(s);
    return true;
  }

  settingsApplyJson(s, root);
  return true;
}
```

**Note:** `settingsApplyJson` in this plan still accepts the old mode tokens defensively (`"stocks"`/`"usage"`/`"radar"`/`"carousel"` → all map to `MODE_AUTO`), so a POST from a cached old web page cannot put the device into a deleted mode. The mapping lives where `mode` is parsed in `settingsApplyJson` — after the parser reads the old token, coerce to `MODE_AUTO` before storing.

- [ ] **Step 4.7: Build (still fails in main.cpp and WebPortal.cpp).**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -10
```

Expected: errors only in `main.cpp` (calls `usageInit`/`usageService`), `WebPortal.cpp` (calls `usageApply`), and possibly `config.h` if `DEFAULT_MODE` is referenced before definition.

- [ ] **Step 4.8: Commit.**

```sh
git add src/Settings.h src/Settings.cpp
git commit -m "feat(v3): schemaVersion=3, UsageSettings{mode,autoRotateSec}, legacy migration

loadSettings migrates pre-v3 config.json once: lifts WiFi/display/clock,
maps any old mode to AUTO, seeds autoRotateSec from legacy carouselSec,
then writes the clean v3 schema."
```

---

## Task 5: main.cpp — wire UsageMode + the AUTO rotation

**Files:**
- Modify: `src/main.cpp`

**Behaviour (spec §3):**
- `AUTO` starts on Codex, toggles every `autoRotateSec` (default 30 s). Re-entering AUTO resets the timer.
- Manual Codex/ZAI sticks on the selected provider.
- Mode + autoRotateSec persist (handled by Settings).

- [ ] **Step 5.1: Replace the carousel block with the AUTO rotator.**

Open `src/main.cpp`. Delete `carouselHas`, `carouselNext`, and the `if (s.mode == MODE_CAROUSEL)` branch in `activeMode` (Plan 1 left a single-mode registry, so this is already mostly empty). Replace the whole carousel section (around lines 48-89) with:

```cpp
// ---- AUTO rotation --------------------------------------------------------
// AUTO toggles the usage renderer's active provider every autoRotateSec.
// Manual CODEX/ZAI sticks on the selected provider. Entering AUTO resets timer.
static uint32_t g_autoSwitch = 0;   // millis() of the last provider toggle

static void applyMode(const Settings& s) {
  switch (s.usage.mode) {
    case MODE_CODEX: g_usageMode.setActiveProvider(PROVIDER_CODEX); break;
    case MODE_ZAI:   g_usageMode.setActiveProvider(PROVIDER_ZAI);   break;
    case MODE_AUTO:
    default: {
      // Start (or restart) on CODEX; reset timer whenever we (re)enter AUTO.
      g_usageMode.setActiveProvider(PROVIDER_CODEX);
      g_autoSwitch = millis();
      break;
    }
  }
}
```

- [ ] **Step 5.2: Call `applyMode` at boot and in the loop.**

In `setup()`, after `for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);` (main.cpp:197), add:

```cpp
  applyMode(g_settings);
```

In `loop()`, replace the `DisplayMode* m = activeMode(g_settings); if (m) m->service(g_settings);` block (main.cpp:239-240) with:

```cpp
  // AUTO rotation: every autoRotateSec, flip provider. Manual modes stay put.
  if (g_settings.usage.mode == MODE_AUTO) {
    if (g_autoSwitch == 0) g_autoSwitch = millis();
    if (millis() - g_autoSwitch >= (uint32_t)g_settings.usage.autoRotateSec * 1000UL) {
      g_autoSwitch = millis();
      // Flip provider.
      UsageProvider cur;
      // Determine current by reading the mode back — simplest is to track our own.
      // We toggle between CODEX and ZAI:
      g_usageMode.toggleAutoProvider();   // see Step 5.3
    }
  }

  DisplayMode* m = nullptr;
  for (size_t i = 0; i < kModeCount; i++) {
    if (kModes[i] == &g_usageMode) { m = kModes[i]; break; }
  }
  if (m) m->service(g_settings);
```

- [ ] **Step 5.3: Add `toggleAutoProvider()` to `UsageMode`.**

In `src/features/usage/UsageMode.h`, add to the public interface:

```cpp
  void toggleAutoProvider();   // flips CODEX <-> ZAI (AUTO mode only)
```

In `UsageMode.cpp`, add:

```cpp
void UsageMode::toggleAutoProvider() {
  active_ = (active_ == PROVIDER_CODEX) ? PROVIDER_ZAI : PROVIDER_CODEX;
  needsFullRedraw_ = true;
}
```

- [ ] **Step 5.4: Add the includes main.cpp needs.**

At the top of `src/main.cpp`, after `#include "UsageMode.h"`:

```cpp
#include "UsageStore.h"
```

- [ ] **Step 5.5: Build.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -10
```

Expected: errors only in `WebPortal.cpp` (calls `usageApply`). Task 6 fixes it.

- [ ] **Step 5.6: Commit.**

```sh
git add src/main.cpp src/features/usage/UsageMode.h src/features/usage/UsageMode.cpp
git commit -m "feat(v3): main.cpp drives AUTO provider rotation on autoRotateSec"
```

---

## Task 6: UsageApi + WebPortal wiring + mDNS rename + WebUI

**Files:**
- Create: `src/features/usage/UsageApi.h` / `.cpp`
- Modify: `src/WebPortal.cpp`
- Modify: `src/Net.cpp` (mDNS rename)
- Modify: `src/webui.h` (new Usage tab)

- [ ] **Step 6.1: Create `src/features/usage/UsageApi.h`.**

```cpp
// UsageApi.h — HTTP push/read handlers for /api/usage.
//
// The Mac usage service POSTs one body per provider here. We validate strictly
// (per UsageStore) and never echo credentials. The device stores NO provider
// token: it only holds the most recent pushed snapshot.
#pragma once
#include <Arduino.h>

// Register handlers on the given server (called from webPortalBegin). The
// server instance lives in WebPortal.cpp; we expose begin() so UsageApi does
// not need its own server.
class WebServerClass;   // forward decl (Platform.h defines the alias)
void usageApiBegin(WebServerClass& server);
```

- [ ] **Step 6.2: Create `src/features/usage/UsageApi.cpp`.**

```cpp
#include "UsageApi.h"
#include "UsageStore.h"
#include "../../Platform.h"   // WebServerClass
#include <ArduinoJson.h>

// File-scope pointer to the server (set once in usageApiBegin). The route
// handlers below are free functions so they do not depend on lambda capture
// lifetime, which is fragile with ESP8266WebServer's THandlerFunction copy.
static WebServerClass* s_server = nullptr;

static void sendUsageOverview() {
  String out;
  g_usageStore.serializeOverview(out);
  s_server->send(200, "application/json", out);
}

static void handleUsagePost() {
  if (!s_server->hasArg("plain")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
    return;
  }
  const String& body = s_server->arg("plain");
  // Peek the provider token to route the push to the right store slot.
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  const char* tok = doc["provider"] | "";
  UsageProvider p;
  if (strcasecmp(tok, "codex") == 0)      p = PROVIDER_CODEX;
  else if (strcasecmp(tok, "zai") == 0)   p = PROVIDER_ZAI;
  else {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad provider\"}");
    return;
  }
  // applyPush validates strictly; last-good snapshot is untouched on failure.
  bool ok = g_usageStore.applyPush(p, body);
  s_server->send(ok ? 200 : 400, "application/json",
                 ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid\"}");
}

void usageApiBegin(WebServerClass& server) {
  s_server = &server;
  server.on("/api/usage", HTTP_GET,  sendUsageOverview);
  server.on("/api/usage", HTTP_POST, handleUsagePost);
}
```

**Note:** the route handlers are plain free functions reading the file-scope `s_server`. This avoids the lifetime footgun of `[&server]` lambda captures with `ESP8266WebServer::on` (the `THandlerFunction` std::function is copied/stored, and a dangling captured reference would crash on the first request after `usageApiBegin` returns). The server instance in `WebPortal.cpp` lives for the program's lifetime, so `s_server` is stable.

- [ ] **Step 6.3: `WebPortal.cpp` — call `usageApiBegin` and drop old usage references.**

In `src/WebPortal.cpp`:
- Replace `#include "UsageClient.h"` with `#include "UsageApi.h"` and `#include "../features/usage/UsageStore.h"`.
- In `handleStatus` (cpp:62-106): replace the `#if WITH_TICKER ... tickers[] #endif` block with a usage overview block:

```cpp
  // Spec §1: "Web status/UI ต้องรายงาน hardware เป็น SmallTV-ultra · ESP8266"
  o["hardware"] = "SmallTV-ultra · ESP8266";
  o["chip"] = "esp8266";
  // Usage overview (read-only; the refresh button must NOT trigger a provider API call).
  String usageJson;
  g_usageStore.serializeOverview(usageJson);
  // Parse it back into the status doc (cheap; small object).
  JsonDocument ud;
  deserializeJson(ud, usageJson);
  o["usage"] = ud.as<JsonObjectConst>();
```

  Also set `o["mode"]` (currently `"ap"`/`"sta"`) — leave that as is; that is the network mode. Add `o["displayMode"]` separately if useful (optional).

- In `webPortalBegin` (cpp:284-314): after the existing `server.on("/api/usage", HTTP_POST, handleUsagePush);` line, **delete** that line (the new `usageApiBegin` registers both GET and POST). Add a call after `server.on("/update", ...)`:

```cpp
  usageApiBegin(server);
```

- Delete the old `handleUsagePush` function (cpp:235-244) and the `handleRefresh` function (cpp:205-210) and its `/api/refresh` route registration (cpp:298) — they referenced deleted Ticker/UsageClient code.

- [ ] **Step 6.4: `Net.cpp` — rename `_clawdmeter._tcp` to `_aiusage._tcp` with the new TXT records.**

In `src/Net.cpp` find the `#if WITH_USAGE` block (cpp:102-110). Replace with:

```cpp
#if WITH_USAGE
        // Discoverable usage-push service so the Mac usage daemon can find and
        // push to every SmallTV-ultra on the LAN (no hardcoded host).
        MDNS.addService("aiusage", "tcp", 80);
        MDNS.addServiceTxt("aiusage", "tcp", "id",     g_hostname.c_str());
        MDNS.addServiceTxt("aiusage", "tcp", "ver",    FW_VERSION);
        MDNS.addServiceTxt("aiusage", "tcp", "path",   "/api/usage");
        MDNS.addServiceTxt("aiusage", "tcp", "schema", "1");
#endif
```

- [ ] **Step 6.5: `webui.h` — replace the Usage tab.**

Open `src/webui.h`. In the `<nav>` of tab buttons (around line 59), make sure the only feature tab is `usage` (Ticker/Radar were deleted in Plan 1 Task 7). In the `<section class="tab" id="usage">` block (around lines 200-208, left empty by Plan 1), insert:

```html
<section class="tab" id="usage">
  <h2>Usage Display</h2>

  <label>Mode</label>
  <select id="usage-mode">
    <option value="codex">Codex</option>
    <option value="zai">Z.ai</option>
    <option value="auto">Auto (rotate)</option>
  </select>

  <label>Auto rotation seconds (5–3600)</label>
  <input type="number" id="usage-rotate" min="5" max="3600" step="1">

  <h3>Codex</h3>
  <div id="usage-codex" class="usage-grid">
    <div><span>5H</span><b id="codex-5h">N/A</b><span id="codex-5h-reset">RESET --</span></div>
    <div><span>Weekly</span><b id="codex-wk">N/A</b><span id="codex-wk-reset">RESET --</span></div>
    <div><span>Age</span><b id="codex-age">--</b><span id="codex-state">--</span></div>
  </div>

  <h3>Z.ai</h3>
  <div id="usage-zai" class="usage-grid">
    <div><span>5H</span><b id="zai-5h">N/A</b><span id="zai-5h-reset">RESET --</span></div>
    <div><span>Weekly</span><b id="zai-wk">N/A</b><span id="zai-wk-reset">RESET --</span></div>
    <div><span>Age</span><b id="zai-age">--</b><span id="zai-state">--</span></div>
  </div>

  <button id="usage-refresh" type="button">Refresh status</button>
  <p class="hint">Refresh only reads the device state. It does NOT trigger a provider API call.</p>
</section>
```

In the JS section:
- In `collect()` (around line 410), add `usage: { mode: ..., autoRotateSec: ... }` by reading `#usage-mode` and `#usage-rotate`.
- In `loadConfig()` (around line 355), populate `#usage-mode` and `#usage-rotate` from `C.usage`, and call a new `loadUsageOverview()` that GETs `/api/status` and fills the `#codex-*`/`#zai-*` cells from `C.usage.providers[]`.
- Add a click handler on `#usage-refresh` that re-runs `loadUsageOverview()` only — NO POST. Add a comment: `// read-only: never fire a provider API from the UI`.
- Add a small CSS block for `.usage-grid` (a 2-column grid; the `<b>` is the big % and the `<span>` is the small reset/state label).

- [ ] **Step 6.6: Build all three envs.**

```sh
uvx --from platformio platformio run -e smalltv_ultra        2>&1 | tail -10
uvx --from platformio platformio run -e smalltv_ultra_loader 2>&1 | tail -6
uvx --from platformio platformio run -e clock_usb            2>&1 | tail -6
```

Expected: all three `[SUCCESS]`. If `clock_usb` fails, you touched `usb_clock.cpp` by mistake — revert.

- [ ] **Step 6.7: Commit.**

```sh
git add src/features/usage/UsageApi.h src/features/usage/UsageApi.cpp src/WebPortal.cpp src/Net.cpp src/webui.h
git commit -m "feat(v3): UsageApi (/api/usage GET+POST), _aiusage._tcp mDNS, WebUI tab

POST /api/usage validates per-provider and never touches last-good on 400.
GET  /api/usage returns both providers + age + stale. The WebUI refresh
button only reads state — it never fires a provider API call."
```

---

## Task 7: Host-side unit tests for the push/validation/migration logic

**Why:** AGENTS.md says flash decisions need visual confirmation, but the validation/migration logic is pure and testable on the host. The spec's test plan (§host tests) lists 13 cases; we cover the firmware-side ones here with a host harness. The Mac service has its own tests in Plan 3.

**Files:**
- Create: `tests/test_usage_store.py` — a host test that cross-checks the C++ validation logic by re-implementing the same rules in Python and asserting the spec's push-API test cases. (Direct C++ unit testing would need a PlatformIO native env; the project has none. A Python mirror is the pragmatic choice and matches the existing `tests/test_clock_tools.py` style.)
- Create: `tests/_usage_store_rules.py` — the single-source-of-truth rule mirror (used by the test).

- [ ] **Step 7.1: Create `tests/_usage_store_rules.py` — the validation rules mirrored from UsageStore.cpp.**

```python
"""Mirror of src/features/usage/UsageStore.cpp push validation.
Kept in sync by tests/test_usage_store.py. If you change the C++, change this too."""
from typing import Optional

def validate_push(provider_route: str, body: dict) -> bool:
    """Return True iff UsageStore::applyPush would accept this body."""
    if body.get("v") != 1:
        return False
    tok = body.get("provider")
    if not isinstance(tok, str):
        return False
    if tok.lower() != provider_route:
        return False
    saw_pct = False
    for pk, rk in (("five_hour_used_pct", "five_hour_reset_min"),
                   ("weekly_used_pct",   "weekly_reset_min")):
        pct = body.get(pk)
        if pct is not None:
            if not isinstance(pct, int) or isinstance(pct, bool):
                return False
            if pct < 0 or pct > 100:
                return False
            saw_pct = True
        rst = body.get(rk)
        if rst is not None:
            if not isinstance(rst, int) or isinstance(rst, bool):
                return False
            if rst < 0 or rst > 65535:
                return False
    return saw_pct
```

- [ ] **Step 7.2: Create `tests/test_usage_store.py` with the spec's push-API test cases.**

```python
"""Host tests for the /api/usage push validation (spec §host tests).
These test the Python mirror in _usage_store_rules.py. The C++ in
src/features/usage/UsageStore.cpp MUST match these rules."""
import unittest
from _usage_store_rules import validate_push

class PushValidationTests(unittest.TestCase):
    def test_codex_weekly_only(self):
        b = {"v":1,"provider":"codex","five_hour_used_pct":None,
             "five_hour_reset_min":None,"weekly_used_pct":42,"weekly_reset_min":600}
        self.assertTrue(validate_push("codex", b))

    def test_missing_window_is_null(self):
        # 5H absent -> accepted, marked N/A on device
        b = {"v":1,"provider":"codex","weekly_used_pct":42,"weekly_reset_min":600}
        self.assertTrue(validate_push("codex", b))

    def test_zai_full(self):
        b = {"v":1,"provider":"zai","five_hour_used_pct":5,
             "five_hour_reset_min":120,"weekly_used_pct":91,"weekly_reset_min":5890}
        self.assertTrue(validate_push("zai", b))

    def test_bad_version(self):
        b = {"v":2,"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("codex", b))

    def test_missing_version(self):
        b = {"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("codex", b))

    def test_bad_provider_token(self):
        b = {"v":1,"provider":"claude","weekly_used_pct":42}
        self.assertFalse(validate_push("claude", b))

    def test_provider_token_mismatch(self):
        # body says codex, route says zai -> reject (state untouched)
        b = {"v":1,"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("zai", b))

    def test_no_window(self):
        b = {"v":1,"provider":"codex"}
        self.assertFalse(validate_push("codex", b))

    def test_pct_out_of_range(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":101}
        self.assertFalse(validate_push("codex", b))

    def test_reset_out_of_range(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":42,"weekly_reset_min":70000}
        self.assertFalse(validate_push("codex", b))

    def test_pct_is_bool_rejected(self):
        # JSON true/false must not be accepted as int (Python bool is int subclass)
        b = {"v":1,"provider":"codex","weekly_used_pct":True}
        self.assertFalse(validate_push("codex", b))

    def test_pct_zero_ok(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":0}
        self.assertTrue(validate_push("codex", b))

    def test_pct_hundred_ok(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":100}
        self.assertTrue(validate_push("codex", b))

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 7.3: Run the tests.**

```sh
cd tests && python -m unittest test_usage_store -v && cd ..
```

Expected: `OK` with 13 tests.

- [ ] **Step 7.4: Commit.**

```sh
git add tests/_usage_store_rules.py tests/test_usage_store.py
git commit -m "test(v3): host tests for /api/usage push validation (13 spec cases)"
```

---

## Task 8: Settings migration host test

**Files:**
- Modify: `tests/test_usage_store.py` (add a migration test class) OR create `tests/test_settings_migration.py`

**Spec coverage:** spec §host tests: "legacy config migration preserves WiFi/display and writes schema 3".

- [ ] **Step 8.1: Create `tests/test_settings_migration.py` — mirrors the `loadSettings` migration rules.**

```python
"""Host tests for the Settings v3 migration (spec §host tests).
Mirrors src/Settings.cpp loadSettings() migration rules."""
import unittest

def migrate_v2_to_v3(old: dict) -> dict:
    """Apply the same lift/normalize that loadSettings does for fileVer<3."""
    out = {"schemaVersion": 3}
    for k in ("hostname", "apSsid", "apPass", "httpTimeout",
              "brightness", "autoBrightness", "backlightInverted", "rotation"):
        if k in old:
            out[k] = old[k]
    # WiFi list (authoritative when present).
    if "wifi" in old:
        out["wifi"] = old["wifi"]
    elif "staSsid" in old:
        # legacy single-network mirror
        out["wifi"] = [{"ssid": old["staSsid"], "pass": old.get("staPass", "")}]
    # Clock slice (kept verbatim).
    if "clock" in old:
        out["clock"] = old["clock"]
    # Mode: any old token -> AUTO.
    out["usage"] = {"mode": "auto"}
    # carouselSec -> autoRotateSec, clamped to 5..3600.
    cs = old.get("carouselSec", 30)
    out["usage"]["autoRotateSec"] = max(5, min(3600, int(cs)))
    return out

class MigrationTests(unittest.TestCase):
    def test_lifts_wifi_display_clock(self):
        old = {
            "hostname": "smalltv-ab12", "staSsid": "home", "staPass": "secret",
            "brightness": 70, "rotation": 2, "httpTimeout": 8000,
            "backlightInverted": True, "autoBrightness": False,
            "mode": "stocks", "carouselSec": 45,
            "ticker": {"webhookUrl": "x"}, "usage": {"usageUrl": "y"},
            "clock": {"tz": "Asia/Bangkok"},
        }
        new = migrate_v2_to_v3(old)
        self.assertEqual(new["schemaVersion"], 3)
        self.assertEqual(new["hostname"], "smalltv-ab12")
        self.assertEqual(new["wifi"], [{"ssid": "home", "pass": "secret"}])
        self.assertEqual(new["brightness"], 70)
        self.assertEqual(new["rotation"], 2)
        self.assertEqual(new["clock"], {"tz": "Asia/Bangkok"})
        self.assertEqual(new["usage"], {"mode": "auto", "autoRotateSec": 45})

    def test_drops_deleted_keys(self):
        old = {"mode": "radar", "carouselSec": 30,
               "ticker": {"symbols": []}, "radar": {"lat": 0},
               "usage": {"usageUrl": "x"}, "carouselTicker": True}
        new = migrate_v2_to_v3(old)
        for banned in ("ticker", "radar", "usageUrl", "carouselTicker",
                       "staSsid", "staPass", "mode"):
            self.assertNotIn(banned, new, f"{banned} leaked into v3 schema")
        # The old top-level "usage" object is gone; only the new usage slice remains.
        self.assertEqual(set(new["usage"].keys()), {"mode", "autoRotateSec"})

    def test_any_old_mode_maps_to_auto(self):
        for tok in ("stocks", "usage", "radar", "carousel"):
            old = {"mode": tok, "carouselSec": 30}
            new = migrate_v2_to_v3(old)
            self.assertEqual(new["usage"]["mode"], "auto")

    def test_clamps_autorotate(self):
        self.assertEqual(migrate_v2_to_v3({"carouselSec": 1})["usage"]["autoRotateSec"], 5)
        self.assertEqual(migrate_v2_to_v3({"carouselSec": 99999})["usage"]["autoRotateSec"], 3600)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 8.2: Run.**

```sh
cd tests && python -m unittest test_settings_migration -v && cd ..
```

Expected: `OK` with 4 tests.

- [ ] **Step 8.3: Commit.**

```sh
git add tests/test_settings_migration.py
git commit -m "test(v3): host tests for Settings v3 migration (4 spec cases)"
```

---

## Task 9: Build-gate verification + spec acceptance (firmware half)

**Files:** none modified — verification only.

- [ ] **Step 9.1: Spec §build gates.**

```sh
uvx --from platformio platformio run -e smalltv_ultra        2>&1 | tail -6
uvx --from platformio platformio run -e smalltv_ultra_loader 2>&1 | tail -6
uvx --from platformio platformio run -e clock_usb            2>&1 | tail -6
git diff --check
uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin 2>&1 | tail -15
```

Expected: all `[SUCCESS]`; `git diff --check` clean; image-info reports ESP8266 / 4 MB / DIO / valid checksum.

- [ ] **Step 9.2: No project-code compiler warnings.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | grep -i "warning:" | grep -v "elf2bin\|SyntaxWarning\|platformio/packages" || echo "no project warnings"
```

Expected: `no project warnings`.

- [ ] **Step 9.3: RAM check (spec: "RAM should not exceed ~47 KB baseline").**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | grep -E "RAM:|Flash:"
```

Expected: free RAM is comparable to or better than the Plan-1 baseline. Record the numbers.

- [ ] **Step 9.4: Run both host test files.**

```sh
cd tests && python -m unittest test_usage_store test_settings_migration -v && cd ..
```

Expected: 17 tests, `OK`.

- [ ] **Step 9.5: Do NOT tag, flash, or publish.** Physical acceptance (flashing the device, visually confirming the three modes, the LIVE/STALE behaviour, and AUTO rotation) happens at the end of Plan 3, because Plan 3 is what produces the Mac pusher that feeds the device. Flashing now would show `STALE` on every provider (no pusher running).

- [ ] **Step 9.6: No commit (verification only).**

---

## Self-Review

- [ ] **Coverage of spec §3 (Usage domain and display modes):**
  - `DisplayMode` enum (CODEX/ZAI/AUTO)? ✓ Task 1.
  - `UsageWindow`/`ProviderUsage` structs (available/usedPct/resetMin/everReceived/lastOkMs)? ✓ Task 1.
  - `UsageStore` owns snapshots + validation + freshness + countdown? ✓ Task 2.
  - `UsageMode` takes provider id + theme, renders one layout? ✓ Task 3.
  - AUTO starts on Codex, toggles every 30 s, resets timer on re-entry? ✓ Task 5.
  - Manual Codex/ZAI sticks? ✓ Task 5.
  - mode + autoRotateSec persist? ✓ Task 4 (UsageSettings + Settings I/O).
  - Layout 240×240 (y=0..35 / 42..116 / 124..198 / 204..239)? ✓ Task 3.
  - Cards show used %, bar, reset countdown; N/A → `RESET --`? ✓ Task 3.
  - Palette exact? — **Task 1.2 mandates recomputing each RGB565 from the `#RRGGBB`; the plan lists the formula. Flag for the implementer to not ship approximations.**
  - Threshold 0–69/70–89/90–100? ✓ Task 3 (`barColorFor`).
  - Redraw rules (full on mode/provider/rotation/config; status+cards on new data; countdown/age on minute change; never full panel per second)? ✓ Task 3.

- [ ] **Coverage of spec §4 (Settings migration):**
  - schemaVersion=3 + new schema keys? ✓ Task 4.
  - mode `codex`/`zai`/`auto`? ✓ Task 4.
  - autoRotateSec 5..3600? ✓ Task 4.
  - Legacy mode → auto; carouselSec → autoRotateSec; keep WiFi/display/clock/httpTimeout; drop ticker/radar/usageUrl/carousel flags; save once; no deleted keys in output? ✓ Task 4 + Task 8 host test.

- [ ] **Coverage of spec §Public Interfaces:**
  - Push API POST `/api/usage` with the exact JSON, validation rules, 400-doesn't-touch-state, `{"ok":true}` on success? ✓ Tasks 2, 6.
  - Read API GET `/api/usage` returns both providers + age_sec + stale, no credentials? ✓ Tasks 2, 6.
  - mDNS `_aiusage._tcp` port 80, TXT id/ver/path/schema=1? ✓ Task 6.
  - Web UI: Mode selector, autoRotateSec, both providers' 5H/Weekly/age/LIVE/STALE, refresh reads only, no credential fields, no Ticker/Radar/Claude/Usage-URL? ✓ Task 6.

- [ ] **Placeholder scan:** the RGB565 values in Task 1.2 are explicitly flagged "must recompute" — that is a concrete instruction, not a placeholder. Every other step has concrete code or commands.

- [ ] **Type consistency:**
  - `UsageProvider`/`PROVIDER_CODEX`/`PROVIDER_ZAI`/`PROVIDER_COUNT` used identically in `UsageStore.h` (Task 1), `UsageStore.cpp` (Task 2), `UsageMode.h/.cpp` (Task 3), `main.cpp` (Task 5), `UsageApi.cpp` (Task 6). ✓
  - `DisplayMode` enum values `MODE_CODEX`/`MODE_ZAI`/`MODE_AUTO` defined in config.h (Task 1.1) and used in `UsageMode::modeConst()` (Task 3.2 — returns `MODE_AUTO` as sentinel), `UsageSettings::fromJson` (Task 4.2), `main.cpp::applyMode` (Task 5.1). ✓
  - `g_usageStore` declared `extern` in `UsageStore.h`, defined in `UsageStore.cpp`. ✓
  - `g_usageMode` declared `extern` in `UsageMode.h`, defined in `UsageMode.cpp`. ✓
  - `usageApiBegin(WebServerClass&)` declared in `UsageApi.h`, called in `WebPortal.cpp`. ✓
  - `usageProviderColor`/`usageProviderLabel` declared in `UsageStore.h`, defined in `UsageStore.cpp`, used in `UsageMode.cpp`. ✓

---

## Done criteria for Plan 2

1. `smalltv_ultra`, `smalltv_ultra_loader`, `clock_usb` all build with no project-code warnings.
2. `image-info` on the production image: ESP8266 / 4 MB / DIO / valid.
3. `python -m unittest test_usage_store test_settings_migration` passes 17 cases.
4. `POST /api/usage` (simulated via the host rule mirror) accepts exactly the spec's valid bodies and rejects exactly the spec's invalid ones.
5. `GET /api/usage` JSON has the spec's shape (schema=1, providers[] with age_sec/stale/five_hour/weekly).
6. mDNS advertises `_aiusage._tcp` with TXT id/ver/path/schema=1.
7. The WebUI Usage tab has Mode + autoRotateSec + both providers' live readouts and a refresh button that does not POST.
8. Physical acceptance (flashing + visual confirmation of CODEX/ZAI/AUTO + LIVE/STALE + AUTO rotation) is deferred to the end of Plan 3.

**Hand-off to Plan 3:** `docs/superpowers/plans/2026-07-21-v3-plan3-mac-service.md`.
