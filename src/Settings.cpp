#include "Settings.h"
#include "Platform.h"   // platformChipId() for the unique default hostname
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

// 12-char alphanumeric (no ambiguous chars like O/0/I/1) for the Setup AP.
// Uses ESP.getChipId() + micros() as entropy; NOT cryptographic, but the
// threat model is "friend on the same Wi-Fi", not a targeted attacker.
static String randomApPass() {
  static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  uint32_t seed = ESP.getChipId() ^ micros() ^ (millis() << 8);
  randomSeed(seed);
  String out;
  for (uint8_t i = 0; i < 12; i++) {
    out += (char)ALPHABET[random(sizeof(ALPHABET) - 1)];
  }
  return out;
}

// ===========================================================================
// Usage slice
// ===========================================================================
void UsageSettings::setDefaults() {
  mode          = DEFAULT_MODE;
  autoRotateSec = USAGE_AUTOROTATE_SEC;
  codexSec      = 0;   // 0 = fall back to autoRotateSec
  zaiSec        = 0;
  systemSec     = 0;
}

void UsageSettings::toJson(JsonObject o) const {
  o["mode"]          = (mode == MODE_ZAI)    ? "zai"
                     : (mode == MODE_CODEX)  ? "codex"
                     : (mode == MODE_SYSTEM) ? "system" : "auto";
  o["autoRotateSec"] = autoRotateSec;
  o["codexSec"]      = codexSec;
  o["zaiSec"]        = zaiSec;
  o["systemSec"]     = systemSec;
}

void UsageSettings::fromJson(JsonObjectConst o) {
  if (o["mode"].is<const char*>()) {
    String m = o["mode"].as<String>();
    mode = m.equalsIgnoreCase("zai")    ? MODE_ZAI
         : m.equalsIgnoreCase("codex")  ? MODE_CODEX
         : m.equalsIgnoreCase("system") ? MODE_SYSTEM : MODE_AUTO;
  }
  if (o["autoRotateSec"].is<int>())
    autoRotateSec = (uint16_t)constrain((int)o["autoRotateSec"],
                                        USAGE_AUTOROTATE_MIN, USAGE_AUTOROTATE_MAX);
  // Per-provider dwell (0 = inherit autoRotateSec). Min 2s so very fast
  // rotations (3s) are expressible.
  if (o["codexSec"].is<int>())  codexSec  = (uint16_t)constrain((int)o["codexSec"],  0, USAGE_AUTOROTATE_MAX);
  if (o["zaiSec"].is<int>())    zaiSec    = (uint16_t)constrain((int)o["zaiSec"],    0, USAGE_AUTOROTATE_MAX);
  if (o["systemSec"].is<int>()) systemSec = (uint16_t)constrain((int)o["systemSec"], 0, USAGE_AUTOROTATE_MAX);
}

// ===========================================================================
// Clock / night mode slice
// ===========================================================================
static uint16_t hhmmToMin(const char* s, uint16_t fallback) {
  if (!s || !s[0]) return fallback;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return (uint16_t)(h * 60 + m);
}
static String minToHhmm(uint16_t v) {
  if (v > 1439) v = 0;
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(v / 60), (unsigned)(v % 60));
  return String(b);
}

void ClockSettings::setDefaults() {
  tz            = DEFAULT_TZ_NAME;
  tzPosix       = DEFAULT_TZ_POSIX;
  nightEnabled  = DEFAULT_NIGHT_ENABLED;
  nightStartMin = DEFAULT_NIGHT_START_MIN;
  nightEndMin   = DEFAULT_NIGHT_END_MIN;
  nightLevel    = DEFAULT_NIGHT_LEVEL;
}

void ClockSettings::toJson(JsonObject o) const {
  o["tz"]           = tz;
  o["tzPosix"]      = tzPosix;
  o["nightEnabled"] = nightEnabled;
  o["nightStart"]   = minToHhmm(nightStartMin);
  o["nightEnd"]     = minToHhmm(nightEndMin);
  o["nightLevel"]   = nightLevel;
}

void ClockSettings::fromJson(JsonObjectConst o) {
  if (o["tz"].is<const char*>())          tz = o["tz"].as<String>();
  if (o["tzPosix"].is<const char*>())     tzPosix = o["tzPosix"].as<String>();
  if (o["nightEnabled"].is<bool>())       nightEnabled = o["nightEnabled"];
  if (o["nightStart"].is<const char*>())  nightStartMin = hhmmToMin(o["nightStart"], nightStartMin);
  if (o["nightEnd"].is<const char*>())    nightEndMin   = hhmmToMin(o["nightEnd"], nightEndMin);
  if (o["nightLevel"].is<int>())          nightLevel = constrain((int)o["nightLevel"], 0, 100);
}

// ===========================================================================
// Top-level settings
// ===========================================================================
void Settings::setDefaults() {
  schemaVersion = 4;
  wifiCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_NETS; i++) {
    wifi[i].ssid = "";
    wifi[i].pass = "";
  }
  apSsid  = DEFAULT_AP_SSID;
  apPass  = DEFAULT_AP_PASS;
  if (apPass.length() < 8) {
    // Spec v3.1: a fresh/unpaired device must show a random ≥12-char AP
    // password on screen so the owner can read it; an open AP would let
    // any neighbour pair first. This runs once on a truly fresh chip.
    apPass = randomApPass();
  }
  pairedH1 = "";   // unpaired until /api/pair runs (Plan 2 Task 4)
  // Unique per device so several SmallTVs on one network don't collide on
  // mDNS out of the box. A hostname saved in config.json overrides this.
  hostname = String(DEFAULT_HOSTNAME) + "-" + String(platformChipId() & 0xFFFF, HEX);

  mode = DEFAULT_MODE;
  // carouselSec is kept only as the migration source field: legacy pre-v3
  // config.json files seed usage.autoRotateSec from this value. New installs
  // use USAGE_AUTOROTATE_SEC directly.
  carouselSec = USAGE_AUTOROTATE_SEC;
  httpTimeout = DEFAULT_HTTP_TIMEOUT;

  brightness = DEFAULT_BRIGHTNESS;
  autoBrightness = false;
  backlightInverted = TFT_BL_DEFAULT_INVERTED;
  rotation = 0;

  usage.setDefaults();
  clock.setDefaults();
}

// ---------------------------------------------------------------------------
bool settingsBegin() {
  if (LittleFS.begin()) return true;
  // First boot on a fresh chip: format then mount.
  if (LittleFS.format() && LittleFS.begin()) return true;
  return false;
}

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
    // settingsApplyJson handles both the legacy and v3 layouts and the legacy
    // top-level mode tokens; after it runs we normalize the v3 fields.
    settingsApplyJson(s, root);
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

  // v3 -> v4: pairing state introduced. The device MUST boot unpaired
  // (forced re-pair per spec). Drop any stale auth field a hand-edited
  // config might carry; the user pairs fresh from Setup AP.
  if (fileVer < 4) {
    settingsApplyJson(s, root);
    s.pairedH1     = "";          // forced re-pair
    s.schemaVersion = 4;
    saveSettings(s);
    return true;
  }

  settingsApplyJson(s, root);
  return true;
}

bool saveSettings(const Settings& s) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(s, root, /*includeSecrets=*/true);

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

// ---------------------------------------------------------------------------
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["schemaVersion"] = s.schemaVersion;
  root["hostname"]      = s.hostname;

  // WiFi networks. Passwords only reach the config file, never the web API.
  JsonArray wf = root["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    JsonObject e = wf.add<JsonObject>();
    e["ssid"]    = s.wifi[i].ssid;
    e["passSet"] = s.wifi[i].pass.length() > 0;
    if (includeSecrets) e["pass"] = s.wifi[i].pass;
  }

  root["apSsid"]     = s.apSsid;
  root["apPassSet"]  = s.apPass.length() > 0;
  if (includeSecrets) {
    root["apPass"]   = s.apPass;
    if (s.pairedH1.length()) root["pairedH1"] = s.pairedH1;   // hash only, never key
  }

  // Shared HTTP/display (no legacy staSsid/staPass mirror, no carouselSec,
  // no carouselTicker/Usage/Radar, no top-level mode token).
  root["httpTimeout"]       = s.httpTimeout;
  root["brightness"]        = s.brightness;
  root["autoBrightness"]    = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"]          = s.rotation;

  // Feature slices. Authoritative display mode lives in usage.mode.
  s.usage.toJson(root["usage"].to<JsonObject>());
  s.clock.toJson(root["clock"].to<JsonObject>());
}

// Apply only the keys that are present (partial update friendly). Accepts both
// the v3 nested layout and the legacy flat layout (so a pre-v3 config.json or
// a cached old web page still applies). Legacy mode tokens (stocks/usage/radar/
// carousel) and the legacy top-level `mode` field coerce to MODE_AUTO; the
// authoritative display mode lives in usage.mode.
void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["schemaVersion"].is<int>()) s.schemaVersion = (uint16_t)root["schemaVersion"].as<int>();
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();

  if (root["wifi"].is<JsonArrayConst>()) {
    // The list is authoritative when present (order = try priority, missing
    // row = deletion). A blank password keeps the stored one, matched by SSID
    // so rows survive reordering.
    WifiCred old[MAX_WIFI_NETS];
    uint8_t oldCount = s.wifiCount;
    for (uint8_t i = 0; i < oldCount; i++) old[i] = s.wifi[i];

    s.wifiCount = 0;
    for (JsonObjectConst e : root["wifi"].as<JsonArrayConst>()) {
      if (s.wifiCount >= MAX_WIFI_NETS) break;
      const char* ssid = e["ssid"] | "";
      if (!ssid[0]) continue;                // skip blank rows
      WifiCred& dst = s.wifi[s.wifiCount];
      dst.ssid = ssid;
      const char* pass = e["pass"] | "";
      dst.pass = pass;
      if (!pass[0])
        for (uint8_t i = 0; i < oldCount; i++)
          if (old[i].ssid == dst.ssid) { dst.pass = old[i].pass; break; }
      s.wifiCount++;
    }
  } else if (root["staSsid"].is<const char*>()) {
    // Legacy single-network layout (pre-2.4 config.json or an old cached web
    // page): it becomes/updates the primary network, extras stay untouched.
    String ssid = root["staSsid"].as<String>();
    if (ssid.length()) {
      s.wifi[0].ssid = ssid;
      if (root["staPass"].is<const char*>()) {
        String p = root["staPass"].as<String>();
        if (p.length() > 0) s.wifi[0].pass = p;   // blank = keep
      }
      if (s.wifiCount < 1) s.wifiCount = 1;
    }
  }
  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  // AP password: apply as-is when present (empty allowed => open AP).
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();
  if (root["pairedH1"].is<const char*>()) s.pairedH1 = root["pairedH1"].as<String>();

  // Legacy top-level "mode" token: stocks/usage/radar/carousel all map to
  // MODE_AUTO (the authoritative display mode lives in usage.mode now). Any
  // v3 token (codex/zai/auto) is honoured only via usage{} below.
  if (root["mode"].is<const char*>()) s.mode = MODE_AUTO;

  // Legacy carouselSec is kept on the in-memory struct so a direct legacy POST
  // still seeds autoRotateSec. loadSettings handles the file-level migration.
  if (root["carouselSec"].is<int>()) {
    int cs = constrain((int)root["carouselSec"], USAGE_AUTOROTATE_MIN, USAGE_AUTOROTATE_MAX);
    s.carouselSec = (uint16_t)cs;
  }

  if (root["httpTimeout"].is<int>())        s.httpTimeout = constrain((int)root["httpTimeout"], 1000, 20000);
  if (root["brightness"].is<int>())         s.brightness = constrain((int)root["brightness"], 0, 100);
  if (root["autoBrightness"].is<bool>())    s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>())           s.rotation = (uint8_t)(((int)root["rotation"]) & 3);

  // Feature slices: prefer the nested object; fall back to the top level so a
  // legacy flat config.json (or a legacy POST) still applies.
  JsonObjectConst u = root["usage"].is<JsonObjectConst>() ? root["usage"].as<JsonObjectConst>() : root;
  s.usage.fromJson(u);
  if (root["clock"].is<JsonObjectConst>()) s.clock.fromJson(root["clock"].as<JsonObjectConst>());
}
