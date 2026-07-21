# Mac Vitals + Weather/AQI/Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two new display screens to the `smalltv_ultra` Wi-Fi firmware — a Mac Vitals dashboard (6-metric grid) and a Weather+AQI+Clock+Calendar screen — fed by two new Mac adapters over the existing `POST /api/usage` push pipeline.

**Architecture:** Extend the existing provider model (Approach A from the spec). VITALS and WEATHER are added as providers 4 & 5 in `UsageStore`, handled as render branches in `UsageMode::service()` exactly like SYSTEM. The Mac service gains two adapters (`vitals_adapter.py`, `weather_adapter.py`) that return the same dict shape and are pushed through the existing loop. No new API routes, no new store classes — reuse the proven push/validate/stale plumbing.

**Tech Stack:** C++11 (Arduino core, ESP8266, Arduino_GFX Library), Python 3.11+ (Mac adapters, `psutil`, `urllib`), PlatformIO build, pytest-style `unittest` host tests, open-meteo REST APIs.

**Spec:** `docs/superpowers/specs/2026-07-22-vitals-weather-design.md`

---

## File Structure

**Firmware (modify):**
- `src/config.h` — add `PROVIDER_VITALS`/`PROVIDER_WEATHER` to `UsageProvider`; add `MODE_VITALS`/`MODE_WEATHER` to `UiMode`.
- `src/features/usage/UsageStore.h` — extend `ProviderUsage` with new optional fields; bump `PROVIDER_COUNT` to 5.
- `src/features/usage/UsageStore.cpp` — validate new optional fields; extend `serializeOverview`; add colors/labels for new providers.
- `src/features/usage/UsageMode.h` — widen dirty-tracker arrays to `PROVIDER_COUNT` (5).
- `src/features/usage/UsageMode.cpp` — add VITALS (grid 2×3) and WEATHER (clock hero + mini calendar) render branches.
- `src/features/usage/UsageApi.cpp` — route `"vitals"`/`"weather"` provider tokens.
- `src/Settings.h` — add `WeatherSettings` slice; bump schema version comment.
- `src/Settings.cpp` — defaults, JSON ser/des, schema-5 migration (additive: missing `weather` → Bangkok defaults).
- `src/main.cpp` — `applyMode()` handle `MODE_VITALS`/`MODE_WEATHER`.
- `src/webui.h` — add dropdown options + Weather settings card.

**Mac (create/modify):**
- `tools/vitals_adapter.py` — create (psutil: CPU/RAM/SSD/battery/uptime; temp=None on Apple Silicon).
- `tools/weather_adapter.py` — create (open-meteo + open-meteo AQI).
- `tools/wifi_usage_service.py` — register vitals (60 s) + weather (600 s) providers.
- `tools/wifi-usage.toml.example` — add `[weather]` section with lat/lon.

**Tests (modify/create):**
- `tests/_usage_store_rules.py` — fix stale routes (add `system`); add `vitals`/`weather`; validate new optional fields.
- `tests/test_usage_store.py` — cases for vitals/weather/system accept+reject.
- `tests/test_vitals_adapter.py` — create.
- `tests/test_weather_adapter.py` — create.

---

## Task 1: Update Python validation mirror (spec source-of-truth)

The Python mirror `tests/_usage_store_rules.py` is currently **stale** (only knows `codex`/`zai`, missing `system`). Fix it first so it is the source of truth the C++ must match, then add the new providers and optional-field validation.

**Files:**
- Modify: `tests/_usage_store_rules.py`

- [ ] **Step 1: Rewrite `_usage_store_rules.py` with all 5 routes + new field validation**

Replace the entire file contents with:

```python
"""Mirror of src/features/usage/UsageStore.cpp push validation.
Kept in sync by tests/test_usage_store.py. If you change the C++, change this too.

Routes (must match UsageApi.cpp):
  codex, zai, system, vitals, weather

Per-provider field meanings (the struct reuses slots; the renderer interprets):
  five_hour_used_pct: codex/zai=5H%, system=CPU%, vitals=CPU%, weather=temp°C
  weekly_used_pct:    codex/zai=Weekly%, system=RAM%, vitals=RAM%, weather=AQI index
  extra_pct:          system=SSD%, vitals=SSD%, weather=temp_max°C
  temp_c:             vitals=Mac temp (-127..127, optional)
  battery_pct:        vitals=battery (0..100, optional)
  uptime_min:         vitals=uptime (0..65535, optional)
  weather_code:       weather=WMO code (0..99, optional)
  aqi_pm25:           weather=PM2.5 µg/m³ (0..255, optional)
  temp_min:           weather=daily low °C (-127..127, optional)
"""
from typing import Optional

# Valid provider routes (must match UsageApi.cpp's routing table).
_VALID_ROUTES = ("codex", "zai", "system", "vitals", "weather")

# Optional fields and their validation: (key, min, max)
# None bounds = no range check (only type check).
_OPTIONAL_INT_FIELDS = {
    "temp_c":       (-127, 127),
    "battery_pct":  (0, 100),
    "uptime_min":   (0, 65535),
    "weather_code": (0, 99),
    "aqi_pm25":     (0, 255),
    "temp_min":     (-127, 127),
    "temp_max":     (-127, 127),
}


def _is_int(v) -> bool:
    """Accept int but NOT bool (Python bool is a subclass of int)."""
    return isinstance(v, int) and not isinstance(v, bool)


def validate_push(provider_route: str, body: dict) -> bool:
    """Return True iff a POST /api/usage with this body would be accepted.

    Mirrors the full accept path: UsageApi.cpp's route filter plus
    UsageStore::applyPush's strict body validation."""
    if provider_route not in _VALID_ROUTES:
        return False
    if body.get("v") != 1:
        return False
    tok = body.get("provider")
    if not isinstance(tok, str):
        return False
    if tok.lower() != provider_route:
        return False
    saw_pct = False
    # Two window slots (five_hour / weekly): pct 0..100, reset_min 0..65535.
    for pk, rk in (("five_hour_used_pct", "five_hour_reset_min"),
                   ("weekly_used_pct",   "weekly_reset_min")):
        pct = body.get(pk)
        if pct is not None:
            if not _is_int(pct):
                return False
            if pct < 0 or pct > 100:
                return False
            saw_pct = True
        rst = body.get(rk)
        if rst is not None:
            if not _is_int(rst):
                return False
            if rst < 0 or rst > 65535:
                return False
    # extra_pct: optional, 0..100 (system SSD, vitals SSD, weather temp_max).
    extra = body.get("extra_pct")
    if extra is not None:
        if not _is_int(extra) or extra < 0 or extra > 100:
            return False
        saw_pct = True
    # New optional fields with per-field ranges.
    for key, (lo, hi) in _OPTIONAL_INT_FIELDS.items():
        v = body.get(key)
        if v is not None:
            if not _is_int(v):
                return False
            if v < lo or v > hi:
                return False
            saw_pct = True
    return saw_pct
```

- [ ] **Step 2: Run the existing test suite to see what passes/fails**

Run: `cd /Users/night/Desktop/swc-digital && uv run python -m pytest tests/test_usage_store.py -v 2>&1 | tail -20`

Expected: existing codex/zai tests still PASS. (No new test failures yet — we have not added vitals/weather test cases.)

- [ ] **Step 3: Commit**

```bash
git add tests/_usage_store_rules.py
git commit -m "test: update push-validation mirror — add system/vitals/weather routes

The Python mirror of UsageStore validation had fallen behind (only codex/zai).
Adds system, vitals, weather routes and validation for the new optional fields
(temp_c, battery_pct, uptime_min, weather_code, aqi_pm25, temp_min, temp_max).
This is the source of truth the C++ in Task 3 must match."
```

---

## Task 2: Add test cases for new providers

**Files:**
- Modify: `tests/test_usage_store.py`

- [ ] **Step 1: Append new test cases for system/vitals/weather accept+reject**

Add this block at the end of `tests/test_usage_store.py` (inside the existing `PushValidationTests` class — match indentation of other methods, 4 spaces):

```python
    # ---- system (was missing from the mirror — now covered) ----
    def test_system_full(self):
        b = {"v": 1, "provider": "system",
             "five_hour_used_pct": 42, "weekly_used_pct": 68, "extra_pct": 71}
        self.assertTrue(validate_push("system", b))

    def test_system_bad_extra(self):
        b = {"v": 1, "provider": "system",
             "five_hour_used_pct": 42, "extra_pct": 150}
        self.assertFalse(validate_push("system", b))

    # ---- vitals ----
    def test_vitals_full(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "weekly_used_pct": 68, "extra_pct": 71,
             "temp_c": 54, "battery_pct": 100, "uptime_min": 134}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_temp_na_is_null(self):
        # Apple Silicon: temp_c is null (not sent) — must still accept.
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "weekly_used_pct": 68}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_bad_temp(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "temp_c": 200}
        self.assertFalse(validate_push("vitals", b))

    def test_vitals_neg_temp_ok(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "temp_c": -10}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_bad_uptime(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "uptime_min": 70000}
        self.assertFalse(validate_push("vitals", b))

    # ---- weather ----
    def test_weather_full(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "weekly_used_pct": 87,
             "weather_code": 3, "temp_min": 26, "temp_max": 34, "aqi_pm25": 25}
        self.assertTrue(validate_push("weather", b))

    def test_weather_bad_code(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "weather_code": 150}
        self.assertFalse(validate_push("weather", b))

    def test_weather_bad_pm25(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "aqi_pm25": 300}
        self.assertFalse(validate_push("weather", b))

    def test_weather_bool_rejected(self):
        # bool must not sneak through as int (Python gotcha).
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "aqi_pm25": True}
        self.assertFalse(validate_push("weather", b))
```

- [ ] **Step 2: Run the tests — all should PASS**

Run: `cd /Users/night/Desktop/swc-digital && uv run python -m pytest tests/test_usage_store.py -v 2>&1 | tail -25`

Expected: ALL PASS (old + new). The mirror from Task 1 already implements the validation logic.

- [ ] **Step 3: Commit**

```bash
git add tests/test_usage_store.py
git commit -m "test: add push-validation cases for system/vitals/weather providers"
```

---

## Task 3: Firmware — extend enums in config.h

**Files:**
- Modify: `src/config.h:46-51` (the `UiMode` enum)

- [ ] **Step 1: Add MODE_VITALS and MODE_WEATHER to UiMode**

In `src/config.h`, replace the `UiMode` enum block:

```cpp
enum UiMode : uint8_t {
  MODE_CODEX  = 0,
  MODE_ZAI    = 1,
  MODE_AUTO   = 2,
  MODE_SYSTEM = 3,
};
```

with:

```cpp
enum UiMode : uint8_t {
  MODE_CODEX   = 0,
  MODE_ZAI     = 1,
  MODE_AUTO    = 2,
  MODE_SYSTEM  = 3,
  MODE_VITALS  = 4,
  MODE_WEATHER = 5,
};
```

- [ ] **Step 2: Verify the file still parses (no build yet — just a sanity grep)**

Run: `cd /Users/night/Desktop/swc-digital && grep -n "MODE_" src/config.h`

Expected: shows `MODE_VITALS = 4` and `MODE_WEATHER = 5` in the list.

- [ ] **Step 3: Commit**

```bash
git add src/config.h
git commit -m "feat(fw): add MODE_VITALS and MODE_WEATHER to UiMode enum"
```

---

## Task 4: Firmware — extend UsageProvider enum in UsageStore.h

**Files:**
- Modify: `src/features/usage/UsageStore.h`

- [ ] **Step 1: Add PROVIDER_VITALS and PROVIDER_WEATHER; bump PROVIDER_COUNT**

Replace the `UsageProvider` enum:

```cpp
enum UsageProvider : uint8_t {
  PROVIDER_CODEX  = 0,
  PROVIDER_ZAI    = 1,
  PROVIDER_SYSTEM = 2,
  PROVIDER_COUNT  = 3,
};
```

with:

```cpp
enum UsageProvider : uint8_t {
  PROVIDER_CODEX  = 0,
  PROVIDER_ZAI    = 1,
  PROVIDER_SYSTEM = 2,
  PROVIDER_VITALS = 3,
  PROVIDER_WEATHER= 4,
  PROVIDER_COUNT  = 5,
};
```

- [ ] **Step 2: Extend ProviderUsage struct with new optional fields**

Replace the `ProviderUsage` struct:

```cpp
struct ProviderUsage {
  UsageWindow fiveHour;
  UsageWindow weekly;
  bool        everReceived;   // has any valid push landed for this provider?
  uint32_t    lastOkMs;       // millis() of the last accepted push
  // SYSTEM-only third metric (SSD). 0xFF = not provided. Carried as an
  // optional JSON field so the 2-window schema stays the same for the
  // AI providers (which never set it).
  uint8_t     extraPct;       // 0..100, or 0xFF when unavailable
  // VITALS + WEATHER optional fields (0xFF / 0x80 / 0xFFFF = N/A).
  // VITALS: tempC=Mac temp (0x80=N/A), batteryPct, uptimeMin.
  // WEATHER: tempC=temp_min, extraPct=temp_max, weatherCode (WMO), aqiPm25.
  int8_t      tempC;          // signed: -127..127; 0x80 (-128) = N/A
  uint8_t     batteryPct;     // 0..100, 0xFF = N/A
  uint16_t    uptimeMin;      // 0..65535, 0xFFFF = N/A
  uint8_t     weatherCode;    // WMO 0..99, 0xFF = N/A
  uint8_t     aqiPm25;        // 0..255, 0xFF = N/A
};
```

- [ ] **Step 3: Commit**

```bash
git add src/features/usage/UsageStore.h
git commit -m "feat(fw): extend UsageProvider enum + ProviderUsage struct

Adds PROVIDER_VITALS (3) and PROVIDER_WEATHER (4); PROVIDER_COUNT=5.
Extends ProviderUsage with tempC, batteryPct, uptimeMin, weatherCode,
aqiPm25 optional fields. ~20 extra bytes/provider × 5 = ~100 bytes total."
```

---

## Task 5: Firmware — UsageStore.cpp validation + defaults + labels

**Files:**
- Modify: `src/features/usage/UsageStore.cpp`

- [ ] **Step 1: Initialize the new fields to N/A in `begin()`**

In `UsageStore::begin()`, inside the `for` loop, after `data_[i].extraPct = 0xFF;` add:

```cpp
    data_[i].tempC        = (int8_t)0x80;   // -128 = N/A
    data_[i].batteryPct   = 0xFF;
    data_[i].uptimeMin    = 0xFFFF;
    data_[i].weatherCode  = 0xFF;
    data_[i].aqiPm25      = 0xFF;
```

- [ ] **Step 2: Reset new fields on each push (in `applyPush`, next to `next.extraPct = 0xFF;`)**

Add after `next.extraPct = 0xFF;`:

```cpp
  next.tempC       = (int8_t)0x80;
  next.batteryPct  = 0xFF;
  next.uptimeMin   = 0xFFFF;
  next.weatherCode = 0xFF;
  next.aqiPm25     = 0xFF;
```

- [ ] **Step 3: Add validation + parsing helper for the new optional fields**

After the existing `extra_pct` parsing block (the `JsonVariantConst extra = root["extra_pct"];` ... block) and before `if (!sawPct) return false;`, insert:

```cpp
  // Optional new fields. Each validated against its range; a present-but-
  // out-of-range value rejects the whole push (matches the Python mirror).
  // temp_c is signed (Mac temp / weather temp_min).
  {
    JsonVariantConst t = root["temp_c"];
    if (!t.isNull()) {
      if (!t.is<int>()) return false;
      int v = t.as<int>();
      if (v < -127 || v > 127) return false;
      next.tempC = (int8_t)v;
      sawPct = true;
    }
  }
  {
    JsonVariantConst b = root["battery_pct"];
    if (!b.isNull()) {
      if (!b.is<int>()) return false;
      int v = b.as<int>();
      if (v < 0 || v > 100) return false;
      next.batteryPct = (uint8_t)v;
      sawPct = true;
    }
  }
  {
    JsonVariantConst u = root["uptime_min"];
    if (!u.isNull()) {
      if (!u.is<int>()) return false;
      int v = u.as<int>();
      if (v < 0 || v > 65535) return false;
      next.uptimeMin = (uint16_t)v;
      sawPct = true;
    }
  }
  {
    JsonVariantConst w = root["weather_code"];
    if (!w.isNull()) {
      if (!w.is<int>()) return false;
      int v = w.as<int>();
      if (v < 0 || v > 99) return false;
      next.weatherCode = (uint8_t)v;
      sawPct = true;
    }
  }
  {
    JsonVariantConst p = root["aqi_pm25"];
    if (!p.isNull()) {
      if (!p.is<int>()) return false;
      int v = p.as<int>();
      if (v < 0 || v > 255) return false;
      next.aqiPm25 = (uint8_t)v;
      sawPct = true;
    }
  }
  // temp_min / temp_max reuse tempC / extraPct slots respectively for WEATHER.
  {
    JsonVariantConst tmn = root["temp_min"];
    if (!tmn.isNull()) {
      if (!tmn.is<int>()) return false;
      int v = tmn.as<int>();
      if (v < -127 || v > 127) return false;
      next.tempC = (int8_t)v;          // WEATHER temp_min
      sawPct = true;
    }
  }
  {
    JsonVariantConst tmx = root["temp_max"];
    if (!tmx.isNull()) {
      if (!tmx.is<int>()) return false;
      int v = tmx.as<int>();
      if (v < -127 || v > 127) return false;
      next.extraPct = (uint8_t)v;      // WEATHER temp_max (0..100 fits uint8_t)
      sawPct = true;
    }
  }
```

- [ ] **Step 4: Update `serializeOverview()` to emit the new fields per provider**

In `serializeOverview()`, after the existing `if (data_[i].extraPct != 0xFF) po["extra_pct"] = data_[i].extraPct;` line, add:

```cpp
    if (data_[i].tempC != (int8_t)0x80)      po["temp_c"]       = (int)data_[i].tempC;
    if (data_[i].batteryPct != 0xFF)         po["battery_pct"]  = data_[i].batteryPct;
    if (data_[i].uptimeMin != 0xFFFF)        po["uptime_min"]   = (int)data_[i].uptimeMin;
    if (data_[i].weatherCode != 0xFF)        po["weather_code"] = data_[i].weatherCode;
    if (data_[i].aqiPm25 != 0xFF)            po["aqi_pm25"]     = data_[i].aqiPm25;
```

- [ ] **Step 5: Extend the provider-name table in `applyPush` and `serializeOverview`**

Both functions have `static const char* NAMES[PROVIDER_COUNT] = { "codex", "zai", "system" };`. Update both to:

```cpp
  static const char* const NAMES[PROVIDER_COUNT] = { "codex", "zai", "system", "vitals", "weather" };
```

(Note the added `const` after `char*` — match the style in `serializeOverview` which already had it; `applyPush` should be updated to match.)

- [ ] **Step 6: Add color + label for VITALS and WEATHER**

Update `usageProviderColor()`:

```cpp
uint16_t usageProviderColor(UsageProvider p) {
  switch (p) {
    case PROVIDER_CODEX:   return USAGE_COLOR_CODEX;
    case PROVIDER_ZAI:     return USAGE_COLOR_ZAI;
    case PROVIDER_SYSTEM:  return USAGE_COLOR_TEXT;    // neutral white-ish
    case PROVIDER_VITALS:  return USAGE_COLOR_TEXT;    // neutral white-ish
    case PROVIDER_WEATHER: return 0xBDFD;              // #36D6C4 teal (matches Clock face accent)
    default:               return USAGE_COLOR_TEXT;
  }
}
```

Update `usageProviderLabel()`:

```cpp
const char* usageProviderLabel(UsageProvider p) {
  switch (p) {
    case PROVIDER_CODEX:   return "CODEX";
    case PROVIDER_ZAI:     return "Z.AI";
    case PROVIDER_SYSTEM:  return "SYSTEM";
    case PROVIDER_VITALS:  return "MAC";
    case PROVIDER_WEATHER: return "BKK";    // overridden at render time by settings.weather.city
    default:               return "?";
  }
}
```

- [ ] **Step 7: Build to confirm it compiles**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -15`

Expected: `SUCCESS` (ignore `SyntaxWarning` from elf2bin.py — that is a third-party tool warning, not a firmware error per AGENTS.md).

- [ ] **Step 8: Commit**

```bash
git add src/features/usage/UsageStore.cpp
git commit -m "feat(fw): UsageStore validates + serializes vitals/weather fields

Adds parsing for temp_c, battery_pct, uptime_min, weather_code, aqi_pm25,
temp_min, temp_max. Extends NAMES table to 5 providers. Adds VITALS/WEATHER
colors (neutral white / teal) and labels (MAC / BKK)."
```

---

## Task 6: Firmware — UsageApi.cpp route the new provider tokens

**Files:**
- Modify: `src/features/usage/UsageApi.cpp:34-41`

- [ ] **Step 1: Add routing for vitals + weather**

In `handleUsagePost()`, extend the provider-token-to-enum mapping. Replace:

```cpp
  UsageProvider p;
  if (strcasecmp(tok, "codex") == 0)         p = PROVIDER_CODEX;
  else if (strcasecmp(tok, "zai") == 0)      p = PROVIDER_ZAI;
  else if (strcasecmp(tok, "system") == 0)   p = PROVIDER_SYSTEM;
  else {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad provider\"}");
    return;
  }
```

with:

```cpp
  UsageProvider p;
  if (strcasecmp(tok, "codex") == 0)         p = PROVIDER_CODEX;
  else if (strcasecmp(tok, "zai") == 0)      p = PROVIDER_ZAI;
  else if (strcasecmp(tok, "system") == 0)   p = PROVIDER_SYSTEM;
  else if (strcasecmp(tok, "vitals") == 0)   p = PROVIDER_VITALS;
  else if (strcasecmp(tok, "weather") == 0)  p = PROVIDER_WEATHER;
  else {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad provider\"}");
    return;
  }
```

- [ ] **Step 2: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5`

Expected: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add src/features/usage/UsageApi.cpp
git commit -m "feat(fw): route /api/usage for vitals + weather provider tokens"
```

---

## Task 7: Firmware — Settings WeatherSettings slice + schema migration

**Files:**
- Modify: `src/Settings.h`
- Modify: `src/Settings.cpp`

- [ ] **Step 1: Add WeatherSettings struct to Settings.h**

After the `ClockSettings` struct (before the top-level `Settings` struct), add:

```cpp
// ---- Weather slice (3.3.0) -------------------------------------------------
// City label + coordinates. The coordinates are informational on the device
// (title bar shows city); the Mac adapter owns the actual fetch coordinates
// in wifi-usage.toml. Kept in sync manually (see rollout notes in AGENTS.md).
struct WeatherSettings {
  String city;       // short title-bar label, e.g. "BKK"
  String cityName;   // long label, e.g. "Bangkok" (reserved for future use)
  float  lat;        // latitude (informational)
  float  lon;        // longitude (informational)

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObject o);
};
```

- [ ] **Step 2: Add the slice to the top-level Settings struct**

In `struct Settings`, after `ClockSettings clock;`, add:

```cpp
  WeatherSettings weather;
```

- [ ] **Step 3: Add mode-token mapping for vitals/weather in UsageSettings JSON**

In `Settings.cpp`, in `UsageSettings::toJson`, update the `mode` token mapping. Replace:

```cpp
  o["mode"] = (mode == MODE_ZAI)    ? "zai"
            : (mode == MODE_CODEX)  ? "codex"
            : (mode == MODE_SYSTEM) ? "system" : "auto";
```

with:

```cpp
  o["mode"] = (mode == MODE_ZAI)     ? "zai"
            : (mode == MODE_CODEX)   ? "codex"
            : (mode == MODE_SYSTEM)  ? "system"
            : (mode == MODE_VITALS)  ? "vitals"
            : (mode == MODE_WEATHER) ? "weather" : "auto";
```

In `UsageSettings::fromJson`, replace:

```cpp
    mode = m.equalsIgnoreCase("zai")    ? MODE_ZAI
         : m.equalsIgnoreCase("codex")  ? MODE_CODEX
         : m.equalsIgnoreCase("system") ? MODE_SYSTEM : MODE_AUTO;
```

with:

```cpp
    mode = m.equalsIgnoreCase("zai")     ? MODE_ZAI
         : m.equalsIgnoreCase("codex")   ? MODE_CODEX
         : m.equalsIgnoreCase("system")  ? MODE_SYSTEM
         : m.equalsIgnoreCase("vitals")  ? MODE_VITALS
         : m.equalsIgnoreCase("weather") ? MODE_WEATHER : MODE_AUTO;
```

- [ ] **Step 4: Implement WeatherSettings methods**

In `Settings.cpp`, add (near the `ClockSettings` methods):

```cpp
// ---- WeatherSettings ------------------------------------------------------
void WeatherSettings::setDefaults() {
  city     = "BKK";
  cityName = "Bangkok";
  lat      = 13.7563f;
  lon      = 100.5018f;
}

void WeatherSettings::toJson(JsonObject o) const {
  o["city"]     = city;
  o["cityName"] = cityName;
  o["lat"]      = lat;
  o["lon"]      = lon;
}

void WeatherSettings::fromJson(JsonObject o) {
  if (o["city"].is<const char*>())       city     = o["city"].as<String>();
  if (o["cityName"].is<const char*>())   cityName = o["cityName"].as<String>();
  if (o["lat"].is<float>())              lat      = o["lat"].as<float>();
  if (o["lon"].is<float>())              lon      = o["lon"].as<float>();
}
```

- [ ] **Step 5: Wire weather into top-level settingsToJson / settingsApplyJson**

In `settingsToJson`, after the `clock` slice is serialized (find the `s.clock.toJson(...)` call), add:

```cpp
  s.weather.toJson(root["weather"].to<JsonObject>());
```

In `settingsApplyJson`, after the `clock` slice is applied, add:

```cpp
  if (root["weather"].is<JsonObject>())
    s.weather.fromJson(root["weather"]);
```

- [ ] **Step 6: Set defaults + schema migration**

In `Settings::setDefaults()`, bump `schemaVersion` from `4` to `5`, and after `clock.setDefaults();` add:

```cpp
  weather.setDefaults();
```

In `loadSettings` migration block, after the schema-4 migration handling, the additive rule covers schema 5: if the `weather` key is absent from an older file, `settingsApplyJson` simply skips it (the `is<JsonObject>()` check fails) and `setDefaults()` has already filled Bangkok. No explicit migration branch needed — but add a comment where schema versions are compared:

Find the line `uint16_t fileVer = root["schemaVersion"].is<int>() ? ...` and add a comment above it:

```cpp
  // schema 5 (3.3.0): added weather slice. Additive — a missing "weather"
  // key is harmless; defaults (Bangkok) are applied below if absent.
```

- [ ] **Step 7: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5`

Expected: `SUCCESS`.

- [ ] **Step 8: Commit**

```bash
git add src/Settings.h src/Settings.cpp
git commit -m "feat(fw): add WeatherSettings slice + schema-5 migration

WeatherSettings {city, cityName, lat, lon} defaults to Bangkok.
UsageSettings mode token now accepts 'vitals'/'weather'. Schema bumped
4→5; migration is additive (missing slice → Bangkok defaults)."
```

---

## Task 8: Firmware — main.cpp applyMode for new modes

**Files:**
- Modify: `src/main.cpp:42-54` (the `applyMode` function)

- [ ] **Step 1: Handle MODE_VITALS and MODE_WEATHER in applyMode**

Replace the `applyMode` function body's switch:

```cpp
  switch (s.usage.mode) {
    case MODE_CODEX:  g_usageMode.setActiveProvider(PROVIDER_CODEX);  break;
    case MODE_ZAI:    g_usageMode.setActiveProvider(PROVIDER_ZAI);    break;
    case MODE_SYSTEM: g_usageMode.setActiveProvider(PROVIDER_SYSTEM); break;
    case MODE_AUTO:
    default: {
      g_usageMode.setActiveProvider(PROVIDER_CODEX);
      g_autoSwitch = millis();
      break;
    }
  }
```

with:

```cpp
  switch (s.usage.mode) {
    case MODE_CODEX:   g_usageMode.setActiveProvider(PROVIDER_CODEX);   break;
    case MODE_ZAI:     g_usageMode.setActiveProvider(PROVIDER_ZAI);     break;
    case MODE_SYSTEM:  g_usageMode.setActiveProvider(PROVIDER_SYSTEM);  break;
    case MODE_VITALS:  g_usageMode.setActiveProvider(PROVIDER_VITALS);  break;
    case MODE_WEATHER: g_usageMode.setActiveProvider(PROVIDER_WEATHER); break;
    case MODE_AUTO:
    default: {
      g_usageMode.setActiveProvider(PROVIDER_CODEX);
      g_autoSwitch = millis();
      break;
    }
  }
```

- [ ] **Step 2: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5`

Expected: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fw): applyMode handles MODE_VITALS and MODE_WEATHER"
```

---

## Task 9: Firmware — UsageMode.h dirty-tracker arrays

**Files:**
- Modify: `src/features/usage/UsageMode.h:27-34`

- [ ] **Step 1: Add WEATHER dirty trackers**

The existing arrays (`lastFiveHourOk_`, `lastWeeklyOk_`, etc.) are already sized `[PROVIDER_COUNT]` which is now 5, so they auto-grow. Add two new trackers for the clock-second and calendar-day that the WEATHER renderer needs. In the `private:` section, after `lastStale_`, add:

```cpp
  // WEATHER-only dirty trackers (clock + calendar are device-local, not
  // pushed; they change on their own cadence).
  uint8_t  lastClockMin_  = 0xFF;   // last minute rendered (0..59)
  uint8_t  lastClockDay_  = 0xFF;   // last day-of-month rendered (1..31)
  bool     weatherFirstDraw_ = true;
```

- [ ] **Step 2: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5`

Expected: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add src/features/usage/UsageMode.h
git commit -m "feat(fw): add WEATHER dirty trackers (clock min/day, first-draw flag)"
```

---

## Task 10: Firmware — VITALS renderer (Grid 2×3)

**Files:**
- Modify: `src/features/usage/UsageMode.cpp`

This task adds the VITALS render branch. It reuses the existing `drawSystemCard()` helper for the 4 metric cards.

- [ ] **Step 1: Add a battery+uptime banner helper**

Near `drawSystemCard()` (top of `UsageMode.cpp`), add:

```cpp
// VITALS-only: full-width banner with battery + uptime. Two cells side by
// side in one 224-wide card, h=30.
static void drawVitalsBanner(int16_t y, uint8_t batteryPct, uint16_t uptimeMin,
                             bool stale) {
  auto* d = gfxDev();
  d->fillRoundRect(8, y, 224, 30, 5, USAGE_COLOR_CARD);
  // Battery cell (left).
  d->setTextSize(1);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(18, y + 10);
  d->print("BAT");
  d->setTextSize(2);
  if (batteryPct != 0xFF) {
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", batteryPct);
    d->setCursor(48, y + 8);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    d->setCursor(48, y + 8);
    d->print("--");
  }
  // Uptime cell (right). Compact d/h/m.
  d->setTextSize(1);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(140, y + 10);
  d->print("UP");
  d->setTextSize(2);
  d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
  if (uptimeMin != 0xFFFF) {
    char buf[16];
    uint16_t m = uptimeMin;
    uint16_t days = m / 1440; m -= days * 1440;
    uint16_t hrs  = m / 60;   m -= hrs * 60;
    if (days)        snprintf(buf, sizeof(buf), "%ud %uh", days, hrs);
    else if (hrs)    snprintf(buf, sizeof(buf), "%uh %um", hrs, m);
    else             snprintf(buf, sizeof(buf), "%um", m);
    d->setCursor(162, y + 8);
    d->print(buf);
  } else {
    d->setCursor(162, y + 8);
    d->print("--");
  }
}
```

- [ ] **Step 2: Add the VITALS branch in `UsageMode::service()`**

Right after the SYSTEM branch's closing `return;` (the one before the `// Full redraw path.` comment for AI providers), insert:

```cpp
  // ---- VITALS: Grid 2×3 (CPU/RAM/SSD/TEMP cards + BAT/UP banner) ----
  if (active_ == PROVIDER_VITALS) {
    if (needsFullRedraw_) {
      needsFullRedraw_ = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      // Title bar.
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      d->print("MAC");
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // 4 cards: CPU / RAM / SSD / TEMP.
      drawSystemCard(42,  "CPU",  pu.fiveHour.usedPct, pu.fiveHour.available, providerColor, stale);
      drawSystemCard(42,  "RAM",  pu.weekly.usedPct,   pu.weekly.available,   providerColor, stale);
      // Wait — CPU and RAM are both drawn at y=42; fix below: CPU at x=8,
      // RAM at x=124. drawSystemCard always starts at x=8, so we need a
      // side-by-side variant. Use the existing full-width card but split.
      // CORRECTION: drawSystemCard draws full width (224). For a 2-column
      // grid we need a half-width card. Add drawVitalsHalfCard() below.
    }
  }
```

**Important correction:** the above placeholder revealed that `drawSystemCard()` draws a full 224-wide card, but the 2×3 grid needs half-width (108-wide) cards. Step 3 replaces the stub.

- [ ] **Step 3: Replace Step 2's stub with a half-width card helper + correct VITALS branch**

First, add a half-width card helper near `drawSystemCard()`:

```cpp
// VITALS half-width card (108 wide) for the 2×3 grid. Same visual language
// as drawSystemCard but compact: label top-left, big % top-right, slim bar.
static void drawVitalsCard(int16_t x, int16_t y, const char* label,
                           uint8_t pct, bool avail, bool isTemp,
                           uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  d->fillRoundRect(x, y, 108, 56, 5, USAGE_COLOR_CARD);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(x + 8, y + 6);
  d->print(label);
  d->setTextSize(3);
  if (avail) {
    d->setTextColor(barColorFor(pct, providerColor, stale));
    char buf[10];
    if (isTemp) snprintf(buf, sizeof(buf), "%d\xF8", (int8_t)pct);  // \xF8 = '°' approx
    else        snprintf(buf, sizeof(buf), "%u%%", pct);
    int16_t tw = gfxTextW(buf, 3);
    d->setCursor(x + 100 - tw, y + 6);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "--";
    int16_t tw = gfxTextW(na, 3);
    d->setCursor(x + 100 - tw, y + 6);
    d->print(na);
  }
  // Slim bar (only for % metrics, not temp).
  if (!isTemp) {
    const int16_t by = y + 44, bh = 6, bx = x + 8, bw = 92;
    d->fillRoundRect(bx, by, bw, bh, 3, USAGE_COLOR_BG);
    if (avail && pct > 0) {
      int16_t fw = (int16_t)(bw * (uint32_t)pct / 100UL);
      if (fw < 3) fw = 3;
      d->fillRoundRect(bx, by, fw, bh, 3, barColorFor(pct, providerColor, stale));
    }
  }
}
```

Now replace the Step-2 VITALS branch stub entirely with:

```cpp
  // ---- VITALS: Grid 2×3 (CPU/RAM/SSD/TEMP cards + BAT/UP banner) ----
  if (active_ == PROVIDER_VITALS) {
    if (needsFullRedraw_) {
      needsFullRedraw_ = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      d->print("MAC");
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // Row 1: CPU (left) / RAM (right).
      drawVitalsCard(8,   42,  "CPU",  pu.fiveHour.usedPct, pu.fiveHour.available, false, providerColor, stale);
      drawVitalsCard(124, 42,  "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      // Row 2: SSD (left) / TEMP (right).
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(8,   104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      bool tempAvail = (pu.tempC != (int8_t)0x80);
      drawVitalsCard(124, 104, "TEMP", (uint8_t)pu.tempC,   tempAvail,             true,  providerColor, stale);
      // Banner: battery + uptime.
      drawVitalsBanner(166, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
      return;
    }
    // Partial: data changed → repaint the 4 cards + banner.
    if (pu.lastOkMs != lastFiveHourOk_[active_] || stale != lastStale_[active_]) {
      drawVitalsCard(8,   42,  "CPU",  pu.fiveHour.usedPct, pu.fiveHour.available, false, providerColor, stale);
      drawVitalsCard(124, 42,  "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(8,   104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      bool tempAvail = (pu.tempC != (int8_t)0x80);
      drawVitalsCard(124, 104, "TEMP", (uint8_t)pu.tempC,   tempAvail,             true,  providerColor, stale);
      drawVitalsBanner(166, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
    }
    return;
  }
```

- [ ] **Step 4: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -10`

Expected: `SUCCESS`. If you see a warning about `\xF8`, that is fine — it is the degree symbol in the built-in font; verify on-device in the visual test.

- [ ] **Step 5: Commit**

```bash
git add src/features/usage/UsageMode.cpp
git commit -m "feat(fw): VITALS renderer — Grid 2×3 (CPU/RAM/SSD/TEMP + BAT/UP banner)

Reuses drawSystemCard visual language via new drawVitalsCard (half-width)
and drawVitalsBanner helpers. Partial redraw on data change only."
```

---

## Task 11: Firmware — WEATHER renderer (Clock Hero + Mini Calendar)

This is the most complex renderer. It has three independent dirty regions: clock (per-minute), weather block (per-push), calendar (per-day).

**Files:**
- Modify: `src/features/usage/UsageMode.cpp`
- Modify: `src/Clock.h` (to expose local time getters if not already)

- [ ] **Step 1: Check what Clock.h exposes for local time**

Run: `cd /Users/night/Desktop/swc-digital && grep -n "hour\|minute\|day\|month\|year\|localtime\|tm\|time_t\|now" src/Clock.h`

If `Clock.h` exposes `int clockHour()`, `int clockMinute()`, etc., use them. If it only exposes a formatted string, add raw getters. (The plan assumes getters exist or will be added minimally.)

- [ ] **Step 2: Add local-time getters to Clock.h/.cpp if missing**

If Step 1 shows no raw getters, add to `Clock.h`:

```cpp
  // Raw local-time fields for the WEATHER renderer's clock + calendar.
  // Return -1 if the clock has not synced yet (caller shows '--').
  int  clockHour();     // 0..23
  int  clockMinute();   // 0..59
  int  clockDay();      // 1..31
  int  clockMonth();    // 1..12
  int  clockYear();     // e.g. 2026
  int  clockDow();      // 0=Sunday..6=Saturday
  bool clockSynced();   // true iff NTP has confirmed the time
```

And implement in `Clock.cpp` using the existing `time_t now = time(nullptr); struct tm* lt = localtime(&now);` pattern already used in that file. Return -1 when `clockSynced()` is false.

- [ ] **Step 3: Add a WMO-code-to-label helper**

Near the top of `UsageMode.cpp`, add:

```cpp
// WMO weather code → 3-char label (built-in font lacks weather glyphs).
// See open-meteo docs: 0=clear, 1-3=cloudy, 45-48=fog, 51-67=rain,
// 71-77=snow, 80-82=showers, 95-99=thunderstorm.
static const char* wmoLabel(uint8_t code) {
  if (code == 0)                       return "CLR";
  if (code >= 1 && code <= 3)          return "CLD";
  if (code >= 45 && code <= 48)        return "FOG";
  if (code >= 51 && code <= 67)        return "RAIN";
  if (code >= 71 && code <= 77)        return "SNOW";
  if (code >= 80 && code <= 82)        return "SHR";
  if (code >= 95)                      return "STM";
  return "--";
}

// AQI color by european_aqi band.
static uint16_t aqiColor(uint8_t eaqi) {
  if (eaqi < 20)  return 0x150F;   // green #10A37F
  if (eaqi < 40)  return 0xBDFD;   // teal #36D6C4
  if (eaqi < 60)  return 0xFD84;   // amber #FFB020
  if (eaqi < 80)  return 0xFC80;   // orange
  return            0xF28A;        // red #F05252
}
```

- [ ] **Step 4: Add the WEATHER render branch in `service()`**

After the VITALS branch (and before the AI-provider full-redraw path), insert. This is long because it has three sub-painters (clock, weather block, calendar). Insert:

```cpp
  // ---- WEATHER: Clock hero + Mini calendar ----
  if (active_ == PROVIDER_WEATHER) {
    // Local time from device NTP (not pushed).
    int hh = clockHour(), mm = clockMinute(), dd = clockDay();
    bool synced = clockSynced();

    if (needsFullRedraw_ || weatherFirstDraw_) {
      needsFullRedraw_   = false;
      weatherFirstDraw_  = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      // Title bar: city label from settings (or default "BKK").
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      // City label from settings (defaults to "BKK" if empty).
      d->print(s.weather.city.length() ? s.weather.city.c_str() : "BKK");
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // Paint all three regions.
      // (Clock, weather, calendar painted by the partial helpers below
      //  so we just force them by seeding last values.)
      lastClockMin_ = 0xFF;
      lastClockDay_ = 0xFF;
      lastFiveHourOk_[active_] = 0;
    }

    // Region 1: Clock (repaint only when minute changes).
    if (synced && (uint8_t)mm != lastClockMin_) {
      auto* d = gfxDev();
      // Clear clock area (y=42..105).
      d->fillRect(0, 42, 240, 64, USAGE_COLOR_BG);
      // Time HH:MM centered, size 4.
      d->setTextColor(providerColor);
      d->setTextSize(4);
      char tb[8];
      snprintf(tb, sizeof(tb), "%02d:%02d", hh, mm);
      int16_t tw = gfxTextW(tb, 4);
      d->setCursor((240 - tw) / 2, 50);
      d->print(tb);
      // Date below, size 2, muted.
      int mon = clockMonth(), yr = clockYear(), dow = clockDow();
      static const char* DOWS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      static const char* MONS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      char db[24];
      snprintf(db, sizeof(db), "%s %d %s %d",
               (dow>=0&&dow<=6)?DOWS[dow]:"---", dd,
               (mon>=1&&mon<=12)?MONS[mon-1]:"---", yr);
      int16_t dw = gfxTextW(db, 2);
      d->setCursor((240 - dw) / 2, 90);
      d->print(db);
      lastClockMin_ = (uint8_t)mm;
    } else if (!synced && lastClockMin_ != 0xFE) {
      // Not synced: show waiting text once.
      auto* d = gfxDev();
      d->fillRect(0, 42, 240, 64, USAGE_COLOR_BG);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      const char* w = "waiting NTP...";
      int16_t tw = gfxTextW(w, 2);
      d->setCursor((240 - tw) / 2, 65);
      d->print(w);
      lastClockMin_ = 0xFE;
    }

    // Region 2: Weather + AQI (repaint when new push lands).
    if (pu.lastOkMs != lastFiveHourOk_[active_]) {
      auto* d = gfxDev();
      // Card y=112..160.
      d->fillRoundRect(8, 112, 224, 48, 5, USAGE_COLOR_CARD);
      // Left half: temp + condition.
      d->setTextColor(providerColor);
      d->setTextSize(3);
      char tb[8];
      snprintf(tb, sizeof(tb), "%u\xF8", pu.fiveHour.usedPct);  // temp °C
      d->setCursor(18, 118);
      d->print(tb);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(1);
      d->setCursor(18, 146);
      if (pu.weatherCode != 0xFF) d->print(wmoLabel(pu.weatherCode));
      else                         d->print("--");
      // Divider.
      d->drawFastVLine(120, 118, 36, USAGE_COLOR_BG);
      // Right half: AQI index + PM2.5.
      uint8_t eaqi = pu.weekly.usedPct;
      d->setTextColor(aqiColor(eaqi));
      d->setTextSize(3);
      char ab[8];
      snprintf(ab, sizeof(ab), "%u", eaqi);
      int16_t aw = gfxTextW(ab, 3);
      d->setCursor(232 - aw, 118);
      d->print(ab);
      d->setTextSize(1);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setCursor(132, 118);
      d->print("AQI");
      if (pu.aqiPm25 != 0xFF) {
        char pb[16];
        snprintf(pb, sizeof(pb), "PM2.5 %u", pu.aqiPm25);
        d->setCursor(132, 146);
        d->print(pb);
      }
      lastFiveHourOk_[active_] = pu.lastOkMs;
    }

    // Region 3: Mini calendar week strip (repaint when day changes).
    if (synced && (uint8_t)dd != lastClockDay_) {
      auto* d = gfxDev();
      // Card y=166..226.
      d->fillRoundRect(8, 166, 224, 60, 5, USAGE_COLOR_CARD);
      int dow = clockDow();   // 0=Sun..6=Sat
      int day = clockDay();
      // Compute Monday of this week: JS-style. weekday: 0=Sun.
      // We want Mon-Sun strip with today highlighted.
      // Offset from Monday: (dow+6)%7
      int off = (dow + 6) % 7;
      // Day-of-month for each slot: day - off + i
      static const char* DOW3 = "MTWTFSS";
      d->setTextSize(1);
      for (int i = 0; i < 7; i++) {
        int x = 16 + i * 31;
        // DOW label.
        d->setTextColor(USAGE_COLOR_MUTED);
        d->setCursor(x, 172);
        char dl[2] = {DOW3[i], 0};
        d->print(dl);
        // Day number.
        int slotDay = day - off + i;
        bool isToday = (i == off);
        if (isToday) {
          d->fillRoundRect(x - 2, 184, 24, 20, 3, providerColor);
          d->setTextColor(USAGE_COLOR_BG);
        } else {
          d->setTextColor(USAGE_COLOR_TEXT);
        }
        d->setTextSize(2);
        char db[4];
        snprintf(db, sizeof(db), "%d", slotDay);
        d->setCursor(x, 186);
        d->print(db);
      }
      lastClockDay_ = (uint8_t)dd;
    }

    return;
  }
```

**Note on city label:** the title bar prints `s.weather.city` (falls back to `"BKK"` if empty). The `service(const Settings& s)` signature already carries `s`, so the settings are in scope — no extra plumbing needed.

- [ ] **Step 5: Build**

Run: `cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -15`

Expected: `SUCCESS`. Watch for: unused-variable warnings (e.g. `mon`, `yr` if not used — remove if so), and the `\xF8` byte (degree symbol — verify on device).

- [ ] **Step 6: Commit**

```bash
git add src/features/usage/UsageMode.cpp src/Clock.h src/Clock.cpp
git commit -m "feat(fw): WEATHER renderer — Clock hero + weather block + mini calendar

Three independent dirty regions: clock (per-minute), weather+AQI (per-push),
week-strip calendar (per-day). Never fillScreen per tick. City label from
settings.weather.city. WMO code → 3-char text (CLR/CLD/RAIN/etc.). AQI
color-banded (green/teal/amber/orange/red)."
```

---

## Task 12: Firmware — WebUI dropdown + Weather settings card

**Files:**
- Modify: `src/webui.h`

- [ ] **Step 1: Add VITALS + WEATHER options to the mode `<select>`**

Find the `#usage-mode` select (around line 116) and add two `<option>` lines. The block should read:

```html
   <select id="usage-mode">
    <option value="auto">AUTO (rotate)</option>
    <option value="codex">Codex</option>
    <option value="zai">Z.AI</option>
    <option value="system">System (CPU/RAM/SSD)</option>
    <option value="vitals">Vitals (Mac)</option>
    <option value="weather">Weather + Clock</option>
   </select>
```

(Read the current options first and match the exact wording/style, then append the two new lines before `</select>`.)

- [ ] **Step 2: Add a Weather settings card**

After the existing Display mode card (the one containing `#usage-mode`), add a new card that is shown only when mode=weather. Insert this HTML block (match the surrounding `.card` style):

```html
  <div class="card" id="weather-card" style="display:none">
    <h2>Weather location</h2>
    <label>City label (short, on screen)</label>
    <input type="text" id="weather-city" maxlength="6" placeholder="BKK">
    <label>City name</label>
    <input type="text" id="weather-city-name" maxlength="24" placeholder="Bangkok">
    <label>Latitude</label>
    <input type="text" id="weather-lat" placeholder="13.7563">
    <label>Longitude</label>
    <input type="text" id="weather-lon" placeholder="100.5018">
    <small class="hint">The title bar shows the city label. The Mac adapter uses the lat/lon from <code>wifi-usage.toml</code> to fetch weather — keep them in sync manually.</small>
  </div>
```

- [ ] **Step 3: Wire the weather card into load/save JS**

In the settings-load JS (near where `sv('usage-mode', ...)` is called), add:

```javascript
  var w = c.weather || {};
  if($('weather-city')) sv('weather-city', w.city || 'BKK');
  if($('weather-city-name')) sv('weather-city-name', w.cityName || 'Bangkok');
  if($('weather-lat')) sv('weather-lat', w.lat !== undefined ? w.lat : 13.7563);
  if($('weather-lon')) sv('weather-lon', w.lon !== undefined ? w.lon : 100.5018);
```

In the settings-save JS (near where `o.usage={...}` is built), add:

```javascript
  o.weather = { city: gv('weather-city')||'BKK',
                cityName: gv('weather-city-name')||'Bangkok',
                lat: parseFloat(gv('weather-lat'))||13.7563,
                lon: parseFloat(gv('weather-lon'))||100.5018 };
```

- [ ] **Step 4: Show/hide the weather card based on the mode dropdown**

In the JS that reacts to the mode dropdown change (or in the settings-load function), add:

```javascript
  function syncWeatherCard() {
    var wm = gv('usage-mode');
    var wc = $('weather-card');
    if (wc) wc.style.display = (wm === 'weather') ? '' : 'none';
  }
  // Call on load and on dropdown change.
  syncWeatherCard();
  if($('usage-mode')) $('usage-mode').addEventListener('change', syncWeatherCard);
```

- [ ] **Step 5: Build + flash + visual test**

Run:
```bash
cd /Users/night/Desktop/swc-digital && uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5
uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin 2>&1 | grep -E "Target|Flash size|Image checksum"
```

Expected: build SUCCESS; image-info shows ESP8266, 4 MB, valid checksum.

Flash to the device per AGENTS.md flashing rules and visually confirm:
1. WebUI dropdown shows the 2 new options.
2. Selecting "Vitals (Mac)" shows the grid (with `--` for values until the Mac pushes).
3. Selecting "Weather + Clock" shows the clock (once NTP syncs) + `--` weather.
4. The Weather settings card appears/hides correctly.

- [ ] **Step 6: Commit**

```bash
git add src/webui.h
git commit -m "feat(fw): WebUI — Vitals/Weather dropdown options + Weather settings card

Adds two options to the display-mode select and a Weather location card
(city/cityName/lat/lon) shown only when mode=weather."
```

---

## Task 13: Mac — vitals_adapter.py

**Files:**
- Create: `tools/vitals_adapter.py`

- [ ] **Step 1: Write the test first**

Create `tests/test_vitals_adapter.py`:

```python
"""Tests for the Mac vitals adapter (CPU/RAM/SSD/battery/uptime).

Mocks psutil so the tests run without a real Mac and without blocking on
cpu_percent intervals."""
import unittest
from unittest import mock
import importlib
import sys
import os
import types

# Inject a fake psutil into sys.modules before importing the adapter.
class FakeSensors:
    @staticmethod
    def battery():
        return types.SimpleNamespace(percent=80, power_plugged=True, secsleft=-2)

class FakePsutil:
    @staticmethod
    def cpu_percent(interval=0.0):
        return 42.0
    @staticmethod
    def virtual_memory():
        return types.SimpleNamespace(percent=68)
    @staticmethod
    def disk_usage(path):
        return types.SimpleNamespace(used=710, total=1000)
    @staticmethod
    def boot_time():
        return 1000.0
    @staticmethod
    def sensors_battery():
        return FakeSensors.battery()

class VitalsAdapterTests(unittest.TestCase):
    def setUp(self):
        sys.modules["psutil"] = FakePsutil
        # Force reimport.
        if "vitals_adapter" in sys.modules:
            del sys.modules["vitals_adapter"]
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
        import vitals_adapter
        self.mod = vitals_adapter

    def test_returns_expected_shape(self):
        import time
        with mock.patch("time.time", return_value=2134.0):  # uptime = 1134s = 18.9 -> 18m
            out = self.mod.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 42)
        self.assertEqual(out["weekly"]["used_pct"], 68)
        self.assertEqual(out["extra_pct"], 71)
        self.assertEqual(out["battery_pct"], 80)
        self.assertEqual(out["temp_c"], None)
        self.assertIn("uptime_min", out)

    def test_cpu_clamped(self):
        with mock.patch.object(FakePsutil, "cpu_percent", return_value=150.0):
            out = self.mod.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 100)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test — expect FAIL (module not found)**

Run: `cd /Users/night/Desktop/swc-digital && uv run --with psutil python -m pytest tests/test_vitals_adapter.py -v 2>&1 | tail -10`

Expected: FAIL (`ModuleNotFoundError: vitals_adapter`).

- [ ] **Step 3: Write vitals_adapter.py**

Create `tools/vitals_adapter.py`:

```python
#!/usr/bin/env python3
"""Mac vitals adapter for SWC Digital (3.3+).

Returns the same dict shape as system_stats_adapter so wifi_usage_service.py
can push it without special-casing:

    {
      "five_hour": {"used_pct": <cpu>},     # CPU %
      "weekly":    {"used_pct": <ram>},     # RAM %
      "extra_pct": <ssd>,                   # SSD %
      "temp_c":    <int or None>,           # Mac temp; None on Apple Silicon
      "battery_pct": <0..100 or None>,
      "uptime_min": <int>,
    }

psutil is the only third-party dep. CPU% is a 3-sample average (smooths
spikes). Temperature is unavailable on Apple Silicon via psutil
(sensors_temperatures() is missing) — returns None so the device shows '--'.
"""
from __future__ import annotations

import os
import time


class VitalsError(Exception):
    """Raised on any failure to read vitals."""


def fetch() -> dict:
    """Return the normalised shape. Raise VitalsError on failure."""
    try:
        import psutil
    except ImportError as exc:
        raise VitalsError("psutil not installed") from exc

    try:
        # CPU: 3 samples × 0.5s averaged (same logic as system_stats_adapter).
        samples = [psutil.cpu_percent(interval=0.5) for _ in range(3)]
        cpu = int(round(sum(samples) / len(samples)))
        ram = int(round(psutil.virtual_memory().percent))
        # SSD: use the data volume on macOS so the % matches Finder.
        disk_path = "/System/Volumes/Data"
        if not os.path.exists(disk_path):
            disk_path = "/"
        du = psutil.disk_usage(disk_path)
        ssd = int(round(du.used / du.total * 100))
        # Battery (laptops; desktops return None).
        bat = psutil.sensors_battery()
        battery_pct = int(round(bat.percent)) if bat is not None else None
        # Uptime in minutes.
        uptime_min = int((time.time() - psutil.boot_time()) / 60)
    except Exception as exc:  # noqa: BLE001 — surface any failure
        raise VitalsError(f"psutil read failed: {exc}") from exc

    # Temperature: psutil.sensors_temperatures() does not exist on Apple
    # Silicon (darwin). Return None — the device renders '--'.
    temp_c = None

    cpu = max(0, min(100, cpu))
    ram = max(0, min(100, ram))
    ssd = max(0, min(100, ssd))
    if battery_pct is not None:
        battery_pct = max(0, min(100, battery_pct))
    return {
        "five_hour": {"used_pct": cpu},
        "weekly":    {"used_pct": ram},
        "extra_pct": ssd,
        "temp_c": temp_c,
        "battery_pct": battery_pct,
        "uptime_min": uptime_min,
    }


if __name__ == "__main__":
    import json
    import sys
    try:
        print(json.dumps(fetch()))
    except VitalsError as exc:
        print(f"vitals_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
```

- [ ] **Step 4: Run the test — expect PASS**

Run: `cd /Users/night/Desktop/swc-digital && uv run --with psutil python -m pytest tests/test_vitals_adapter.py -v 2>&1 | tail -10`

Expected: 2 PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/vitals_adapter.py tests/test_vitals_adapter.py
git commit -m "feat(mac): vitals_adapter.py — CPU/RAM/SSD/battery/uptime

psutil-based, same dict shape as system_stats_adapter. Temp is None on
Apple Silicon (psutil has no sensors_temperatures). 3-sample CPU average.
Includes mocked unit tests."
```

---

## Task 14: Mac — weather_adapter.py

**Files:**
- Create: `tools/weather_adapter.py`

- [ ] **Step 1: Write the test first**

Create `tests/test_weather_adapter.py`:

```python
"""Tests for the weather adapter (open-meteo + open-meteo AQI).

Mocks urllib so the tests run offline."""
import json
import unittest
from unittest import mock
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))


def _fake_urlopen(url, timeout):
    # Return fixture based on which API the URL hits.
    if "air-quality-api" in url:
        body = {
            "current": {"pm2_5": 25.2, "european_aqi": 36}
        }
    else:
        body = {
            "current": {"temperature_2m": 28.7, "weather_code": 3},
            "daily": {"temperature_2m_max": [33.4], "temperature_2m_min": [25.9]},
        }
    resp = mock.MagicMock()
    resp.read.return_value = json.dumps(body).encode()
    resp.__enter__ = mock.MagicMock(return_value=resp)
    resp.__exit__ = mock.MagicMock(return_value=False)
    return resp


class WeatherAdapterTests(unittest.TestCase):
    def setUp(self):
        import weather_adapter
        self.mod = weather_adapter

    def test_fetch_returns_expected_shape(self):
        with mock.patch("urllib.request.urlopen", side_effect=_fake_urlopen):
            out = self.mod.fetch(13.7563, 100.5018)
        self.assertEqual(out["five_hour"]["used_pct"], 29)   # 28.7 -> 29
        self.assertEqual(out["weekly"]["used_pct"], 36)      # european_aqi
        self.assertEqual(out["weather_code"], 3)
        self.assertEqual(out["temp_min"], 26)                # 25.9 -> 26
        self.assertEqual(out["temp_max"], 33)                # 33.4 -> 33
        self.assertEqual(out["aqi_pm25"], 25)                # 25.2 -> 25

    def test_fetch_raises_on_http_error(self):
        with mock.patch("urllib.request.urlopen", side_effect=Exception("boom")):
            with self.assertRaises(self.mod.WeatherError):
                self.mod.fetch(13.7563, 100.5018)

    def test_fetch_raises_on_missing_current(self):
        body = {"current": {}}  # empty
        resp = mock.MagicMock()
        resp.read.return_value = json.dumps(body).encode()
        resp.__enter__ = mock.MagicMock(return_value=resp)
        resp.__exit__ = mock.MagicMock(return_value=False)
        with mock.patch("urllib.request.urlopen", return_value=resp):
            with self.assertRaises(self.mod.WeatherError):
                self.mod.fetch(13.7563, 100.5018)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test — expect FAIL**

Run: `cd /Users/night/Desktop/swc-digital && uv run python -m pytest tests/test_weather_adapter.py -v 2>&1 | tail -10`

Expected: FAIL (`ModuleNotFoundError: weather_adapter`).

- [ ] **Step 3: Write weather_adapter.py**

Create `tools/weather_adapter.py`:

```python
#!/usr/bin/env python3
"""Weather adapter for SWC Digital (3.3+).

Fetches current weather + AQI from open-meteo (free, no API key) and returns
the same dict shape as the other adapters so wifi_usage_service.py can push
it without special-casing:

    {
      "five_hour": {"used_pct": <temp_c>},   # temperature °C (rounded)
      "weekly":    {"used_pct": <eaqi>},     # european AQI index
      "weather_code": <wmo>,                 # 0..99
      "temp_min": <int>,                     # daily low °C
      "temp_max": <int>,                     # daily high °C
      "aqi_pm25": <int>,                     # PM2.5 µg/m³
    }

Two endpoints (confirmed working for Bangkok):
  - api.open-meteo.com/v1/forecast
  - air-quality-api.open-meteo.com/v1/air-quality
"""
from __future__ import annotations

import json
import urllib.request


class WeatherError(Exception):
    """Raised on any failure to fetch weather."""


def _get_json(url: str, timeout: int = 10) -> dict:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception as exc:  # noqa: BLE001
        raise WeatherError(f"fetch failed for {url}: {exc}") from exc


def fetch(lat: float, lon: float) -> dict:
    """Fetch current weather + AQI. Raise WeatherError on failure."""
    weather_url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}"
        f"&current=temperature_2m,weather_code"
        f"&daily=temperature_2m_max,temperature_2m_min"
        f"&timezone=auto"
    )
    aqi_url = (
        f"https://air-quality-api.open-meteo.com/v1/air-quality?"
        f"latitude={lat}&longitude={lon}"
        f"&current=pm2_5,european_aqi"
        f"&timezone=auto"
    )
    w = _get_json(weather_url)
    a = _get_json(aqi_url)

    cur = w.get("current") or {}
    daily = w.get("daily") or {}
    acur = a.get("current") or {}
    if "temperature_2m" not in cur or "european_aqi" not in acur:
        raise WeatherError("incomplete API response")

    temp_c = int(round(cur["temperature_2m"]))
    wmo = int(cur.get("weather_code", 0))
    tmax = int(round((daily.get("temperature_2m_max") or [0])[0]))
    tmin = int(round((daily.get("temperature_2m_min") or [0])[0]))
    eaqi = int(round(acur["european_aqi"]))
    pm25 = int(round(acur.get("pm2_5", 0)))

    return {
        "five_hour": {"used_pct": temp_c},
        "weekly":    {"used_pct": eaqi},
        "weather_code": wmo,
        "temp_min": tmin,
        "temp_max": tmax,
        "aqi_pm25": pm25,
    }


if __name__ == "__main__":
    import sys
    try:
        print(json.dumps(fetch(13.7563, 100.5018)))
    except WeatherError as exc:
        print(f"weather_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
```

- [ ] **Step 4: Run the test — expect PASS**

Run: `cd /Users/night/Desktop/swc-digital && uv run python -m pytest tests/test_weather_adapter.py -v 2>&1 | tail -10`

Expected: 3 PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/weather_adapter.py tests/test_weather_adapter.py
git commit -m "feat(mac): weather_adapter.py — open-meteo + AQI

Fetches temp, weather_code, daily hi/lo, european_aqi, pm2.5 from the two
free open-meteo endpoints. Same dict shape as other adapters. Includes
mocked unit tests (offline)."
```

---

## Task 15: Mac — register vitals + weather in wifi_usage_service.py

**Files:**
- Modify: `tools/wifi_usage_service.py`
- Modify: `tools/wifi-usage.toml.example`

- [ ] **Step 1: Add imports + config read for weather coordinates**

At the top of `wifi_usage_service.py`, after the existing adapter imports, add:

```python
import vitals_adapter
import weather_adapter
```

- [ ] **Step 2: Register the two providers in the run loop**

In `cmd_run`, after the `system_state = ProviderState(...)` line, add:

```python
    # Weather fetches every 600 s (10 min) — changes slowly, saves API calls.
    # Implemented as a per-provider interval check inside the fetch wrapper.
    weather_lat = float((cfg.get("weather", {}) or {}).get("lat", 13.7563))
    weather_lon = float((cfg.get("weather", {}) or {}).get("lon", 100.5018))
    weather_last_fetch = [0.0]   # mutable holder for the closure

    def weather_fetch():
        # Throttle: only actually fetch every 600 s.
        now = time.time()
        if now - weather_last_fetch[0] < 600:
            return None   # signal "skip this push" — handled by _step_provider
        weather_last_fetch[0] = now
        return weather_adapter.fetch(weather_lat, weather_lon)

    vitals_state  = ProviderState(name="vitals",  fetch=vitals_adapter.fetch)
    weather_state = ProviderState(name="weather", fetch=weather_fetch)
```

- [ ] **Step 3: Add the two states to the per-iteration loop**

Find the loop `for state in (codex_state, zai_state, system_state):` and extend:

```python
        for state in (codex_state, zai_state, system_state,
                      vitals_state, weather_state):
```

- [ ] **Step 4: Handle the weather "skip" (None) return in _step_provider**

In `_step_provider`, after `windows = state.fetch()`, add a guard for the throttled-skip sentinel:

```python
    try:
        windows = state.fetch()
    except Exception as exc:
        ...
    if windows is None:
        return   # throttled skip (weather 600 s cadence)
```

(Find the exact `windows = state.fetch()` line and add `if windows is None: return` right after the `try/except` block that wraps it.)

- [ ] **Step 5: Add [weather] to wifi-usage.toml.example**

Append to `tools/wifi-usage.toml.example`:

```toml

# ---- Weather adapter (3.3.0) ----
# Coordinates used by weather_adapter.py to fetch open-meteo data.
# Keep in sync with the device's WebUI Weather settings (informational there).
[weather]
lat = 13.7563    # Bangkok
lon = 100.5018
```

- [ ] **Step 6: Test the service runs end-to-end (once mode)**

Run:
```bash
cd /Users/night/Desktop/swc-digital/tools
uv run --with psutil python wifi_usage_service.py run --once 2>&1 | grep -E "event=|error" | head -15
```

Expected: you should see `event=push provider=vitals` and `event=push provider=weather` lines (if the device is reachable), or fetch errors if the device is offline — but no Python exceptions.

- [ ] **Step 7: Commit**

```bash
git add tools/wifi_usage_service.py tools/wifi-usage.toml.example
git commit -m "feat(mac): register vitals (60s) + weather (600s) providers

Vitals runs every loop (60s). Weather throttles to 600s via a closure-based
last-fetch holder returning None on skip. wifi-usage.toml.example gains a
[weather] section with Bangkok coordinates."
```

---

## Task 16: Full integration test on device

This is the AGENTS.md mandatory flashing gate. Do not skip.

**Files:** none (verification only)

- [ ] **Step 1: Full build + image check**

Run:
```bash
cd /Users/night/Desktop/swc-digital
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -5
uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin 2>&1 | grep -E "Target|Flash size|Image checksum"
```

Expected: build SUCCESS; ESP8266, 4 MB, checksum valid. Firmware size < 650 KB.

- [ ] **Step 2: Flash via OTA or serial**

Per AGENTS.md flashing rules. If serial:
```bash
uvx --from esptool esptool \
  --port "$(uv run --with pyserial tools/clockctl.py find-port)" --baud 115200 \
  write-flash 0x0 .pio/build/smalltv_ultra/firmware.bin
```

Confirm esptool reports "hash of data verified."

- [ ] **Step 3: Verify STATUS + identity**

```bash
curl http://<device>.local/api/identity   # (open route)
```

Expected: JSON with the device id.

- [ ] **Step 4: Push a vitals body manually and confirm render**

```bash
curl -X POST http://<device>.local/api/usage \
  -H "Content-Type: application/json" \
  --digest -u "admin:<pairkey>" \
  -d '{"v":1,"provider":"vitals","five_hour_used_pct":42,"weekly_used_pct":68,"extra_pct":71,"temp_c":null,"battery_pct":80,"uptime_min":134}'
```

Expected: `{"ok":true}`. On the device (set mode to Vitals via WebUI): the grid shows CPU 42%, RAM 68%, DISK 71%, TEMP --, BAT 80%, UP 2h 14m.

- [ ] **Step 5: Push a weather body manually and confirm render**

```bash
curl -X POST http://<device>.local/api/usage \
  -H "Content-Type: application/json" \
  --digest -u "admin:<pairkey>" \
  -d '{"v":1,"provider":"weather","five_hour_used_pct":29,"weekly_used_pct":36,"weather_code":3,"temp_min":26,"temp_max":33,"aqi_pm25":25}'
```

Expected: `{"ok":true}`. On the device (set mode to Weather): clock hero (once NTP syncs), weather block (29°, CLD, AQI 36, PM2.5 25), week strip with today highlighted.

- [ ] **Step 6: Visual confirmation (AGENTS.md gate)**

The user visually confirms:
1. Vitals grid: orientation, colour, brightness, readability, no flicker.
2. Weather: clock ticks without full-screen redraw; calendar highlights today; AQI colour band correct.
3. Switching modes via WebUI dropdown works for all 5 options.
4. 180 s after stopping the Mac service → both screens show STALE.

- [ ] **Step 7: Commit any visual-fix tweaks + update AGENTS.md reference state**

If the visual test revealed issues (e.g. degree glyph `\xF8` wrong, calendar offset bug), fix and re-flash. Once green, append a "Verified reference state (2026-07-22, 3.3.0)" block to `AGENTS.md` documenting the new modes.

```bash
git add AGENTS.md
git commit -m "docs: AGENTS.md — verified reference state for 3.3.0 (vitals + weather)"
```

---

## Self-Review Notes (completed during planning)

**Spec coverage:**
- Vitals Grid 2×3 layout → Task 10 ✓
- Weather Clock Hero + Mini Calendar → Task 11 ✓
- WebUI dropdown switching → Tasks 7, 8, 12 ✓
- open-meteo + open-meteo AQI → Task 14 ✓
- WebUI lat/lon config → Task 7 (slice) + Task 12 (card) ✓
- Approach A (extend provider model) → Tasks 4, 5, 6 ✓
- Push schema (vitals + weather bodies) → Tasks 1, 5 ✓
- psutil temp=None on Apple Silicon → Task 13 ✓
- Weather 600 s cadence → Task 15 ✓
- Error handling (STALE after 180 s) → existing plumbing, verified Task 16 ✓
- Schema migration 4→5 → Task 7 ✓

**Risks flagged for implementation:**
- `\xF8` degree glyph in built-in font — verify in Task 16 visual test; fallback to "deg" text.
- Calendar day-offset math (`(dow+6)%7`) — verify visually in Task 16.
- `clockHour/Minute/Day` getters may not exist in Clock.h — Task 11 Step 1-2 handles adding them.
- `int8_t` serialization in JSON (`temp_c` negative) — ArduinoJson handles signed ints; confirmed in Task 5.
