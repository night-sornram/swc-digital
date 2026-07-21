#include "WebPortal.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "Security.h"
#include "UsageApi.h"
#include "features/usage/UsageStore.h"
#include "Clock.h"

// Defined in main.cpp — re-init every mode + force a repaint after a config change.
extern void appInvalidate();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();   // main.cpp: re-resolve effective brightness now

static WebServerClass server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;
static bool             g_selfUpdate = false;   // GitHub self-update requested
static String           g_updateMsg;            // last self-update status/error

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEBUI_HTML);
}

static void handleGetConfig() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feats = root["features"].to<JsonObject>();
  feats["usage"] = (bool)WITH_USAGE;
  // Which chip this build runs on (the UI warns about per-chip limitations).
  root["chip"] = "esp8266";
  sendJson(doc);
}

// Public: device identity only. No credentials, no settings, no usage data.
// This is the route the Mac pairing flow probes to confirm "yes this is the
// device I think it is, with Device ID X".
static void handleIdentity() {
  String out;
  g_security.serializeIdentity(out);
  server.send(200, "application/json", out);
}

// MD5("admin:<realm>:<pairkey>") lowercase hex. Uses MD5Builder from the
// ESP8266 core. Pure function so a future test can mirror it in Python.
static String computeH1(const String& pairkey) {
  MD5Builder md5;
  md5.begin();
  String s = String(SECURITY_USER) + ":" + SECURITY_REALM + ":" + pairkey;
  md5.add(s);
  md5.calculate();
  return md5.toString();   // 32 lowercase hex
}

// AP-only, unpaired-only pairing endpoint. The owner reads the AP password
// off the screen, joins the SmallTV-Setup AP, then POSTs the pair key they
// chose (or that the Mac `pair` command generated for them).
static void handlePair() {
  // Hard guard: AP mode + unpaired. In STA mode this route is unreachable
  // (it is registered but always 404s below). Re-check anyway in case of
  // captive-portal weirdness.
  if (netMode() != NET_AP) { server.send(404, "text/plain", "Not found"); return; }
  if (g_security.paired() || g_security.hasH1()) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"already paired\"}");
    return;
  }
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  // Pair key: 16-char Crockford Base32 (the Mac `pair` command's format).
  // We accept any non-empty printable string >=12 chars so a human-typed
  // passphrase also works.
  const char* key = doc["pairkey"] | "";
  size_t len = strnlen(key, 256);
  if (len < 12 || len > 128) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"pairkey must be 12..128 chars\"}");
    return;
  }
  String h1 = computeH1(String(key));
  if (!g_security.pair(h1)) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"pair failed\"}");
    return;
  }
  // Persist into Settings so it survives reboot.
  S->pairedH1 = h1;
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  // Do NOT reboot: the device stays in AP mode and serves the WebUI with
  // Digest now on, so the same browser session can finish configuration.
}

static void handleStatus() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["repo"] = REPO_URL;
  if (g_updateMsg.length()) o["updateMsg"] = g_updateMsg;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["maxblk"] = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(); if (ts.length()) o["time"] = ts; }
  o["tz"]        = S->clock.tz;
  o["night"]     = clockNightActive();   // dimming now
  o["nightHeld"] = clockNightHeld();      // in the window but waiting for a fresh NTP sync
  o["clockFresh"] = clockTrusted();       // last NTP sync within the trust window
  // Hardware identity and usage overview (read-only; the refresh button must
  // NOT trigger a provider API call).
  o["hardware"] = "SmallTV-ultra · ESP8266";
  o["chip"] = "esp8266";
  {
    String usageJson;
    g_usageStore.serializeOverview(usageJson);
    // Parse it back into the status doc (cheap; small object).
    JsonDocument ud;
    deserializeJson(ud, usageJson);
    o["usage"] = ud.as<JsonObjectConst>();
  }

  sendJson(doc);
}

// Fingerprint of everything network-identity related: the WiFi list and the
// hostname. Changing any of it needs a reboot, because the connection and the
// mDNS registration are established once at boot.
static String netFingerprint(const Settings& s) {
  String f((int)s.wifiCount);
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    f += '\n';
    f += s.wifi[i].ssid;
    f += '\x01';
    f += s.wifi[i].pass;
  }
  f += '\n';
  f += s.hostname;
  return f;
}

static void handlePostConfig() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  String oldNet = netFingerprint(*S);
  uint8_t oldRot = S->rotation;

  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  saveSettings(*S);

  // Live apply (no reboot needed for these)
  clockReapply(*S);         // re-arm SNTP iff the timezone changed
  appApplyBrightness();     // apply effective brightness (respects night/auto/manual)
  if (S->rotation != oldRot) gfxSetRotation(S->rotation);
  appInvalidate();          // re-init every mode + repaint (covers mode/URL/symbol changes)

  bool wifiChanged = netFingerprint(*S) != oldNet;

  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = wifiChanged;
  sendJson(res);

  if (wifiChanged) scheduleReboot(800);
}

static void handleScan() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = !platformScanIsOpen(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleFactory() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

// Full settings backup: stream the persisted config.json verbatim. It includes
// the WiFi passwords — same trust domain as typing them into this page.
static void handleExport() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(404, "text/plain", "no config saved yet"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=smalltv-config.json");
  server.streamFile(f, "application/json");
  f.close();
}

// Restore a backup: apply everything, persist, reboot (WiFi/hostname may change).
static void handleImport() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  scheduleReboot(800);
}

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  OtaLatest r = otaCheckLatest(*S);
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["current"] = FW_VERSION;
  o["ok"] = r.ok;
  o["latest"] = r.tag;
  o["newer"] = r.newer;
  if (!r.ok) o["error"] = r.error;
  sendJson(doc);
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  if (!g_security.authorize(server, netMode() == NET_AP)) return;
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : platformUpdateError().c_str());
  if (ok) scheduleReboot(1200);
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // Auth-check BEFORE any flash write. If the request was not authorised,
    // the handler `handleUpdateDone` never runs (it only runs at the end of a
    // successful POST), so we must refuse here to avoid even a partial flash.
    if (!g_security.authorize(server, netMode() == NET_AP)) {
      Update.end();
      return;
    }
#if defined(SMALLTV_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
  yield();
}

// ---- captive portal -------------------------------------------------------
static void handleNotFound() {
  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
void webPortalBegin(Settings& settings) {
  S = &settings;

  // If the last boot ran a queued GitHub update and failed, surface why
  // (success reboots into the new image before we ever get here).
  g_updateMsg = otaTakeBootResult();

  // ---- public routes (no auth, ever) -------------------------------------
  server.on("/api/identity", HTTP_GET, handleIdentity);
  server.on("/api/pair", HTTP_POST, handlePair);   // AP-only, unpaired-only
  server.on("/",             HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  usageApiBegin(server);   // registers GET + POST /api/usage

  // Common captive-portal probe endpoints
  server.on("/generate_204", handleNotFound);
  server.on("/gen_204", handleNotFound);
  server.on("/hotspot-detect.html", handleNotFound);
  server.on("/connecttest.txt", handleNotFound);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(SMALLTV_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok)         g_updateMsg = "check failed: " + r.error;
    else if (!r.newer) g_updateMsg = "already up to date (" FW_VERSION ")";
    else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "updating...";
      scheduleReboot(400);
    } else {
      g_updateMsg = F("could not queue update (storage error)");
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    g_updateMsg = err.length() ? err : "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
