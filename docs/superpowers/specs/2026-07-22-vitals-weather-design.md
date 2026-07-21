# Design: Mac Vitals Dashboard + Weather/AQI/Clock Screens

**Date:** 2026-07-22
**Firmware target:** `smalltv_ultra` (Wi-Fi usage display)
**Baseline:** v3.2.8 (firmware ~591 KB on 4 MB flash)

## Goal

Add two new display screens to the SmallTV-ultra Wi-Fi firmware:

1. **Mac Vitals Dashboard** — CPU / RAM / SSD / Temp / Battery / Uptime in a 2×3 grid.
2. **Weather + AQI + Clock + Calendar** — Bangkok weather, PM2.5 AQI, live clock (NTP), and a mini week calendar.

Both screens are selected from the WebUI dropdown like the existing CODEX/ZAI/SYSTEM modes. Data is pushed from the Mac service every 60 s (vitals) / 600 s (weather) over the existing `POST /api/usage` endpoint.

## Decisions (locked from brainstorm)

| Decision | Choice |
|---|---|
| Vitals layout | **Grid 2×3** (6 metrics equal weight) |
| Weather layout | **Clock Hero + Mini Calendar** (clock on top, weather+AQI middle, week strip bottom) |
| Screen switching | **WebUI dropdown** (same as current modes — no carousel) |
| Weather data source | **Open-Meteo** (free, no API key) |
| AQI data source | **Open-Meteo Air Quality** (free, no API key) |
| Location config | **WebUI** (lat/lon stored in device config.json) |
| Architecture | **Approach A** — extend UsageStore + UsageMode, add providers, reuse plumbing |

## Architecture: Approach A (extend existing provider model)

VITALS and WEATHER are added as the 4th and 5th entries in the existing `UsageProvider` enum and handled as branches in `UsageMode::service()` — exactly how SYSTEM is handled today. This reuses all existing plumbing: push API, strict validation, dirty-tracking, stale handling, WebUI dropdown.

**Trade-off accepted:** the JSON push body and `ProviderUsage` struct become "one schema that serves several provider meanings" (e.g. `five_hour_used_pct` means CPU for SYSTEM, temperature for WEATHER). The renderer interprets per-provider. This matches how SYSTEM already overloads `extra_pct` for SSD.

### Why not Approach B (separate DisplayMode classes)?

Mode.h was designed for ticker/usage/radar as separate features. But on the ESP8266 (limited RAM, ~591 KB firmware already), duplicating push API routes, store classes, and settings slices for each screen wastes flash and heap. The provider-extension pattern is already proven by SYSTEM (added in 3.2.x without new plumbing) and keeps the diff small.

## Data Schema

### Provider enum (config.h)

```cpp
enum UsageProvider : uint8_t {
  PROVIDER_CODEX  = 0,
  PROVIDER_ZAI    = 1,
  PROVIDER_SYSTEM = 2,
  PROVIDER_VITALS = 3,   // NEW
  PROVIDER_WEATHER= 4,   // NEW
  PROVIDER_COUNT  = 5,   // was 3
};
```

### UiMode enum (config.h)

```cpp
enum UiMode : uint8_t {
  MODE_CODEX  = 0,
  MODE_ZAI    = 1,
  MODE_AUTO   = 2,
  MODE_SYSTEM = 3,
  MODE_VITALS = 4,    // NEW
  MODE_WEATHER= 5,    // NEW
};
```

### Push body: VITALS

`POST /api/usage` with `"provider":"vitals"`:
```json
{
  "v": 1,
  "provider": "vitals",
  "five_hour_used_pct": 42,    // CPU %
  "weekly_used_pct": 68,       // RAM %
  "extra_pct": 71,             // SSD %
  "temp_c": 54,                // optional, -127..200, 0x80 = N/A
  "battery_pct": 100,          // optional, 0..100, 0xFF = N/A
  "uptime_min": 134            // optional, 0..65535
}
```

### Push body: WEATHER

`POST /api/usage` with `"provider":"weather"`:
```json
{
  "v": 1,
  "provider": "weather",
  "five_hour_used_pct": 31,    // temperature °C (0..100 mapped to uint8_t)
  "weekly_used_pct": 87,       // AQI index (european_aqi, 0..200)
  "weather_code": 3,           // WMO code (optional)
  "temp_min": 26,              // daily low °C (optional)
  "temp_max": 34,              // daily high °C (optional)
  "aqi_pm25": 25               // PM2.5 µg/m³ (optional, 0..255)
}
```

### ProviderUsage struct extension (UsageStore.h)

```cpp
struct ProviderUsage {
  UsageWindow fiveHour;
  UsageWindow weekly;
  bool        everReceived;
  uint32_t    lastOkMs;
  uint8_t     extraPct;       // SSD / temp_max
  // NEW optional fields (0xFF / 0x8000 = N/A):
  int8_t      tempC;          // VITALS temp, WEATHER temp_min (signed: -127..127)
  uint8_t     batteryPct;     // VITALS battery (0xFF = N/A)
  uint16_t    uptimeMin;      // VITALS uptime (0xFFFF = N/A)
  uint8_t     weatherCode;    // WEATHER WMO code (0xFF = N/A)
  uint8_t     aqiPm25;        // WEATHER PM2.5 (0xFF = N/A)
};
```

Memory cost per provider: ~20 extra bytes × 5 providers = ~100 bytes total. Negligible on ESP8266.

### Validation rules (UsageStore.cpp `applyPush`)

- Existing validation (v=1, provider token match, 0..100 range on pct) unchanged.
- New optional fields validated only when present: temp_c ∈ [-127,127], battery_pct ∈ [0,100], uptime_min ∈ [0,65535], weather_code ∈ [0,99], aqi_pm25 ∈ [0,255], temp_min/temp_max ∈ [-127,127].
- A push is accepted iff ≥1 metric lands (existing rule; new fields count toward this).

## Firmware Renderer

### VITALS renderer (Grid 2×3)

Branch in `UsageMode::service()` when `active_ == PROVIDER_VITALS`. Layout (240×240):

```
┌────────────────────────────┐ y=0
│ MAC            [LIVE]      │ title bar (h=35)
├──────────┬─────────────────┤ y=42
│  CPU 42% │  RAM 68%        │ row 1 (h=56)
│  ▓▓░░░░  │  ▓▓▓▓░░         │
├──────────┼─────────────────┤ y=104
│ DISK 71% │ TEMP 54°        │ row 2 (h=56)
│  ▓▓▓░░░  │                 │
├────────────────────────────┤ y=166
│ BAT 100% ⚡   UP 2h 14m    │ banner (h=30)
└────────────────────────────┘ y=196
```

- Reuses `drawSystemCard()` helper for the 4 metric cards (CPU/RAM/SSD/TEMP).
- Battery+Uptime banner is a single wide card.
- TEMP shows `--` when `tempC == 0x80` (Apple Silicon can't read temp via psutil).
- Partial redraw: repaint a card only when its value changed (same dirty-tracking pattern as SYSTEM).

### WEATHER renderer (Clock Hero + Mini Calendar)

Branch in `UsageMode::service()` when `active_ == PROVIDER_WEATHER`. Layout (240×240):

```
┌────────────────────────────┐ y=0
│ BKK            LIVE        │ title bar (h=35)
├────────────────────────────┤ y=42
│                            │
│        14:32               │ clock hero (size 4, teal)
│     Tue · 22 Jul 2026      │ date (size 2, muted)
│                            │
├────────────────────────────┤ y=128
│  31°  ☁  │ ● 87  AQI       │ weather+AQI row (h=44)
│  Cloudy  │  PM2.5 25       │
├────────────────────────────┤ y=180
│ M T W T F S S              │ week strip
│ 20 21 [22] 23 24 25 26     │ (today highlighted teal)
│       Jul · W30             │
└────────────────────────────┘ y=226
```

- **Clock** comes from device NTP (`Clock.cpp`), NOT from Mac push. Redraw the time rectangle only when the minute changes (same rule as `usb_clock.cpp`).
- **Weather/AQI** from UsageStore; redraw when `lastOkMs` changes.
- **Calendar** computed from device RTC; redraw only when the day changes.
- WMO weather code → short text + icon glyph (built-in font has ☀ ☁ ☂ in its upper range; if a glyph is missing, fall back to text like "CLR"/"CLD"/"RAIN").
- AQI color: european_aqi < 20 green, 20-40 teal, 40-60 amber, 60-80 orange, >80 red.

### Dirty-tracking for WEATHER

Three independent dirty regions:
1. **Clock** — repaint time digits only when minute changes (cheap: `fillRect` the time box + redraw digits).
2. **Weather block** — repaint when `lastOkMs` changes (new push landed).
3. **Calendar** — repaint the week strip only when `day()` changes.

Never `fillScreen()` every second. Full redraw only on mode enter / rotation change / visual config change.

## Mac Adapters

### `tools/vitals_adapter.py`

Same shape as `system_stats_adapter.py`. Returns:
```python
{
  "five_hour": {"used_pct": <cpu>},      # 3-sample avg
  "weekly":    {"used_pct": <ram>},
  "extra_pct": <ssd>,
  "temp_c": None,                        # N/A on Apple Silicon (psutil has no sensors_temperatures)
  "battery_pct": <0..100 or None>,
  "uptime_min": <int>,
}
```

Temperature is `None` on Apple Silicon (confirmed: `psutil.sensors_temperatures` does not exist). Would require `sudo powermetrics` or a third-party tool — out of scope; show `--` on screen.

### `tools/weather_adapter.py`

```python
{
  "five_hour": {"used_pct": <temp_c>},   # temperature
  "weekly":    {"used_pct": <eaqi>},     # european AQI index
  "weather_code": <wmo>,
  "temp_min": <int>,
  "temp_max": <int>,
  "aqi_pm25": <int>,
}
```

Fetches two open-meteo endpoints (confirmed working for Bangkok 13.75, 100.5):
- `https://api.open-meteo.com/v1/forecast?latitude=<lat>&longitude=<lon>&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto`
- `https://air-quality-api.open-meteo.com/v1/air-quality?latitude=<lat>&longitude=<lon>&current=pm2_5,european_aqi&timezone=auto`

No API key, no auth. On HTTP failure: raise `WeatherError` (service retains cached values, does not push empty).

### `tools/wifi_usage_service.py` changes

Add two `ProviderState` entries to the run loop:
- `vitals_state` — fetch every 60 s (same cadence as codex/zai/system).
- `weather_state` — fetch every 600 s (10 min; weather changes slowly, saves API calls).

The 600 s cadence is implemented as a per-provider interval override: weather's `fetch()` early-returns (skips) unless 600 s have elapsed since its last successful fetch. This avoids a separate timer thread.

## Settings

### New slice: WeatherSettings (Settings.h)

```cpp
struct WeatherSettings {
  String city;       // short label, e.g. "BKK"
  String cityName;   // long label, e.g. "Bangkok"
  float  lat;        // latitude
  float  lon;        // longitude
  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObject o);
};
```

Default: `city="BKK"`, `cityName="Bangkok"`, `lat=13.7563`, `lon=100.5018` (Bangkok).

Added to `struct Settings` alongside `UsageSettings usage` and `ClockSettings clock`.

### config.json shape

```json
{
  "usage": { "mode": "weather", ... },
  "weather": { "city": "BKK", "cityName": "Bangkok", "lat": 13.7563, "lon": 100.5018 }
}
```

Schema version bumped to 5 (migration: if `weather` slice absent, apply Bangkok defaults).

### wifi-usage.toml (Mac side)

The Mac weather adapter reads lat/lon from the device's pushed config? No — simpler: the Mac reads lat/lon from its own `wifi-usage.toml` `[weather]` section (default Bangkok). The device-side lat/lon in `WeatherSettings` is used **only for the title bar label** (city name). The Mac is the weather fetcher; it owns the coordinates.

This avoids round-tripping coordinates device→Mac. If the user changes city in WebUI, they also update `wifi-usage.toml` (documented in rollout).

## WebUI changes (webui.h)

- Add `VITALS` and `WEATHER` options to the `#usage-mode` `<select>`:
  ```html
  <option value="vitals">Vitals (Mac)</option>
  <option value="weather">Weather + Clock</option>
  ```
- Add a Weather settings card (only visible when mode=weather): city, cityName, lat, lon inputs.
- Settings save/load extended to include the `weather` slice.

## Error handling

| Failure | Behavior |
|---|---|
| Weather API HTTP error | Adapter raises; service skips push, retains last-good on device. Screen shows STALE after 180 s (existing rule). |
| Weather API returns partial data | Adapter fills what it can; missing fields sent as null; device shows `--` for missing. |
| psutil missing (vitals) | Adapter raises `VitalsError`; service backs off (existing 2-failure → 2 min backoff). |
| Device temp unavailable | `temp_c: null` in push; renderer shows `--`. |
| Invalid push body | Device rejects with 400 (existing validation); last-good retained. |

## Testing

### Mac adapters (pytest-style, run with uv)
- `vitals_adapter.fetch()` returns dict with expected keys; CPU/RAM/SSD in 0..100; temp_c is None on Apple Silicon.
- `weather_adapter.fetch()` mocks urllib to return fixture JSON; asserts correct mapping.
- Both raise on failure (not return None).

### Firmware (PlatformIO build + visual)
- `pio run -e smalltv_ultra` builds clean, no new warnings.
- Firmware size < 650 KB (baseline 591 KB; budget +60 KB).
- Image checksum valid (`esptool image-info`).
- Flash to device; push a vitals body via curl → VITALS screen renders grid.
- Push a weather body → WEATHER screen renders clock + weather + calendar.
- Visually confirm: no flicker, partial redraw works (clock ticks without full repaint).
- Switch modes via WebUI dropdown → correct screen appears.

### Integration
- Mac service pushes all 5 providers in sequence; device renders the active one.
- Disconnect Mac for 180 s → screen shows STALE.

## Out of scope (YAGNI)

- Temperature on Apple Silicon Macs (needs `sudo powermetrics`; show `--`).
- Multi-day weather forecast (only current + today's high/low).
- Hourly AQI history / sparkline.
- Carousel/auto-rotation between screens (user chose WebUI-select only).
- Geolocation auto-detect (manual lat/lon only).
- OpenWeatherMap fallback (open-meteo is sufficient; field kept extensible).
- Severe weather alerts.

## Risks

1. **WMO weather code → glyph**: built-in GFX font may lack ☀/☁/☂ glyphs. Mitigation: fall back to 3-letter text codes (CLR/CLD/RAIN/FOG). Render spec tested in plan phase.
2. **Calendar computation on ESP8266**: `mktime`/`localtime` available via libc; verify RTC has valid time (skip calendar until NTP-synced, show `--/--` dates).
3. **5 providers × Mac fetch latency**: codex+zai+system+vitals = ~3 s (CPU sample blocks 1.5 s); weather adds one HTTP round-trip (~0.5 s). Total loop < 5 s, well under the 60 s interval. Weather runs at 600 s so it rarely blocks.
4. **Schema migration**: existing devices on schema 4 need weather defaults injected. Migration is additive (missing slice → defaults), no data loss.
