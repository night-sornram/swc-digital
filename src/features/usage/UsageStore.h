// UsageStore.h — snapshots + validation + freshness for three providers.
//
// One instance (g_usageStore) is the single source of truth. The HTTP push API
// writes here; UsageMode reads here. Pull mode was removed in 3.0.0 — the Mac
// service is the only data source.
//
// Providers: CODEX, ZAI (AI usage), SYSTEM (Mac CPU/RAM/SSD via psutil).
// SYSTEM reuses the two-window shape: five_hour = CPU, weekly = RAM.
// SSD is carried via the existing `extra_pct` slot so no schema break.
#pragma once
#include <Arduino.h>

enum UsageProvider : uint8_t {
  PROVIDER_CODEX  = 0,
  PROVIDER_ZAI    = 1,
  PROVIDER_SYSTEM = 2,
  PROVIDER_VITALS = 3,
  PROVIDER_WEATHER= 4,
  PROVIDER_COUNT  = 5,
};

// Freshness tiers, computed from ageMs():
//   LIVE     age < USAGE_STALE_AFTER_MS         normal colors, values shown
//   STALE    STALE_AFTER .. OFFLINE_AFTER       dimmed grey, values retained
//   OFFLINE  age >= USAGE_OFFLINE_AFTER_MS      dimmed grey, values HIDDEN
enum class Freshness : uint8_t { LIVE, STALE, OFFLINE };

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
  bool    stale(UsageProvider p) const;         // true when not LIVE (STALE or OFFLINE)
  Freshness freshness(UsageProvider p) const;   // 3-tier: LIVE / STALE / OFFLINE
  // Format the snapshot as the GET /api/usage JSON (all providers + age + stale).
  void    serializeOverview(String& out) const;
 private:
  ProviderUsage data_[PROVIDER_COUNT];
};

extern UsageStore g_usageStore;

// Theme per provider (color used for the title and the under-threshold bar fill).
uint16_t usageProviderColor(UsageProvider p);
const char* usageProviderLabel(UsageProvider p);  // "CODEX", "Z.AI", "SYSTEM"
