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
