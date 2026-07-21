// Settings.h — persisted configuration (LittleFS /config.json)
//
// Layout is segmented: shared device/network fields live at the top level, and
// each feature owns a nested settings slice (usage, clock). config.json mirrors
// this: { ..shared.., "usage":{...} }.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// One saved WiFi station network. The device keeps up to MAX_WIFI_NETS and
// joins the strongest visible one at boot (hidden SSIDs are tried last).
struct WifiCred {
  String ssid;
  String pass;
};

// ---- Usage display slice (3.0.0) ------------------------------------------
struct UsageSettings {
  uint8_t  mode;            // DisplayMode: MODE_CODEX / MODE_ZAI / MODE_AUTO
  uint16_t autoRotateSec;   // 5..3600 (AUTO provider dwell)

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Clock / night mode slice (device-wide) --------------------------------
struct ClockSettings {
  String   tz;            // IANA display name, e.g. "Europe/Rome" (UI round-trip)
  String   tzPosix;       // POSIX TZ rule the device feeds SNTP
  bool     nightEnabled;  // dim/blank on a nightly schedule
  uint16_t nightStartMin; // minutes since local midnight (0..1439)
  uint16_t nightEndMin;
  uint8_t  nightLevel;    // 0..100, 0 = backlight off

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Top-level settings ----------------------------------------------------
struct Settings {
  uint16_t schemaVersion;   // 3 after migration; 0 on a fresh chip

  // --- WiFi station networks (the device joins one of these) ---
  WifiCred wifi[MAX_WIFI_NETS];
  uint8_t  wifiCount;

  // --- Access point (config / fallback hotspot) ---
  String apSsid;
  String apPass;        // empty => open network
  String hostname;      // mDNS name => http://<hostname>.local

  // --- Active feature (kept for the mode-registry lookup; authoritative
  //     display mode lives in usage.mode) ---
  uint8_t mode;         // MODE_CODEX / MODE_ZAI / MODE_AUTO

  // --- Legacy carousel dwell (kept only as the migration source for
  //     usage.autoRotateSec on pre-v3 config files) ---
  uint16_t carouselSec;

  // --- Shared HTTP / display ---
  uint16_t httpTimeout; // ms
  uint8_t  brightness;        // 0..100 %
  bool     autoBrightness;    // use LDR on A0
  bool     backlightInverted; // active-low backlight
  uint8_t  rotation;          // 0..3 screen orientation

  // --- Feature slices ---
  UsageSettings  usage;
  ClockSettings  clock;

  void setDefaults();
};

// Persistence
bool settingsBegin();                       // mount LittleFS
bool loadSettings(Settings& s);             // false => defaults applied
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);             // wipe file + defaults

// JSON <-> struct. `includeSecrets=false` masks passwords for the web API.
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);
void settingsApplyJson(Settings& s, JsonObjectConst root); // partial update allowed
