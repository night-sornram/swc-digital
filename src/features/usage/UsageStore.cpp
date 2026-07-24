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
    data_[i].extraPct            = 0xFF;
    data_[i].tempC        = (int8_t)0x80;   // -128 = N/A
    data_[i].batteryPct   = 0xFF;
    data_[i].uptimeMin    = 0xFFFF;
    data_[i].weatherCode  = 0xFF;
    data_[i].aqiPm25      = 0xFF;
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
    static const char* const NAMES[PROVIDER_COUNT] = { "codex", "zai", "system", "vitals", "weather" };
    const char* want = NAMES[p];
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
  next.extraPct           = 0xFF;   // SYSTEM-only; AI providers never set it
  next.tempC       = (int8_t)0x80;
  next.batteryPct  = 0xFF;
  next.uptimeMin   = 0xFFFF;
  next.weatherCode = 0xFF;
  next.aqiPm25     = 0xFF;

  bool sawPct = false;
  if (!parseWindow(root, "five_hour_used_pct", "five_hour_reset_min",
                   next.fiveHour, sawPct)) return false;
  if (!parseWindow(root, "weekly_used_pct", "weekly_reset_min",
                   next.weekly, sawPct)) return false;
  // Optional third metric (SYSTEM SSD). Validated like the others.
  JsonVariantConst extra = root["extra_pct"];
  if (!extra.isNull()) {
    if (!extra.is<int>()) return false;
    int v = extra.as<int>();
    if (v < 0 || v > 100) return false;
    next.extraPct = (uint8_t)v;
    sawPct = true;   // counts as "at least one metric landed"
  }
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
  // temp_min maps to tempC (signed slot) for WEATHER.
  {
    JsonVariantConst tmn = root["temp_min"];
    if (!tmn.isNull()) {
      if (!tmn.is<int>()) return false;
      int v = tmn.as<int>();
      if (v < -127 || v > 127) return false;
      next.tempC = (int8_t)v;
      sawPct = true;
    }
  }
  // temp_max maps to extraPct (uint8_t) for WEATHER. Clamp to 0..100 so
  // negative values don't wrap (Bangkok daily highs are always positive).
  {
    JsonVariantConst tmx = root["temp_max"];
    if (!tmx.isNull()) {
      if (!tmx.is<int>()) return false;
      int v = tmx.as<int>();
      if (v < -127 || v > 127) return false;
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      next.extraPct = (uint8_t)v;
      sawPct = true;
    }
  }
  if (!sawPct) return false;   // must have at least one window with a percentage

  // Commit.
  data_[p]              = next;
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
  return freshness(p) != Freshness::LIVE;   // STALE or OFFLINE
}

Freshness UsageStore::freshness(UsageProvider p) const {
  uint32_t age = ageMs(p);
  if (age == 0xFFFFFFFFUL) return Freshness::OFFLINE;  // never received
  if (age > USAGE_OFFLINE_AFTER_MS) return Freshness::OFFLINE;
  if (age > USAGE_STALE_AFTER_MS)   return Freshness::STALE;
  return Freshness::LIVE;
}

void UsageStore::serializeOverview(String& out) const {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["schema"] = 1;
  static const char* const NAMES[PROVIDER_COUNT] = { "codex", "zai", "system", "vitals", "weather" };
  JsonArray arr = root["providers"].to<JsonArray>();
  for (uint8_t i = 0; i < PROVIDER_COUNT; i++) {
    JsonObject po = arr.add<JsonObject>();
    po["provider"] = NAMES[i];
    po["everReceived"] = data_[i].everReceived;
    uint32_t age = ageMs((UsageProvider)i);
    po["age_sec"] = (age == 0xFFFFFFFFUL) ? -1 : (int32_t)(age / 1000UL);
    po["stale"]   = stale((UsageProvider)i);
    po["offline"] = (freshness((UsageProvider)i) == Freshness::OFFLINE);
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
    if (data_[i].extraPct != 0xFF) po["extra_pct"] = data_[i].extraPct;
    if (data_[i].tempC != (int8_t)0x80)      po["temp_c"]       = (int)data_[i].tempC;
    if (data_[i].batteryPct != 0xFF)         po["battery_pct"]  = data_[i].batteryPct;
    if (data_[i].uptimeMin != 0xFFFF)        po["uptime_min"]   = (int)data_[i].uptimeMin;
    if (data_[i].weatherCode != 0xFF)        po["weather_code"] = data_[i].weatherCode;
    if (data_[i].aqiPm25 != 0xFF)            po["aqi_pm25"]     = data_[i].aqiPm25;
  }
  serializeJson(doc, out);
}

uint16_t usageProviderColor(UsageProvider p) {
  switch (p) {
    case PROVIDER_CODEX:   return USAGE_COLOR_CODEX;
    case PROVIDER_ZAI:     return USAGE_COLOR_ZAI;
    case PROVIDER_SYSTEM:  return USAGE_COLOR_TEXT;    // neutral white-ish
    case PROVIDER_VITALS:  return USAGE_COLOR_TEXT;    // neutral white-ish
    case PROVIDER_WEATHER: return 0xBDFD;              // #36D6C4 teal
    default:               return USAGE_COLOR_TEXT;
  }
}
const char* usageProviderLabel(UsageProvider p) {
  switch (p) {
    case PROVIDER_CODEX:   return "CODEX";
    case PROVIDER_ZAI:     return "Z.AI";
    case PROVIDER_SYSTEM:  return "SYSTEM";
    case PROVIDER_VITALS:  return "MAC";
    case PROVIDER_WEATHER: return "BKK";
    default:               return "?";
  }
}
