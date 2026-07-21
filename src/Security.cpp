#include "Security.h"
#include "Platform.h"     // platformChipId()
#include "config.h"       // FW_NAME, FW_VERSION
#include "Net.h"          // netMode()
#include <ArduinoJson.h>

Security g_security;

const char* SECURITY_REALM = "swc-digital";
const char* SECURITY_USER  = "admin";

void Security::begin() {
  // 32-bit chip id -> 8 hex chars. Stable across reboots, IPs, networks.
  uint32_t id = platformChipId();
  snprintf(deviceId_, sizeof(deviceId_), "%08x", (unsigned)id);
  // paired_ and h1_ are filled later (setPaired/setH1/pair). On a fresh boot
  // they are loaded from Settings by the caller (WebPortal/main).
  paired_       = false;
  h1_           = "";
  recoveryOpen_ = false;
}

const char* Security::deviceId() const { return deviceId_; }

bool Security::paired() const         { return paired_; }
void Security::setPaired(bool p)      { paired_ = p; }

void Security::setH1(const String& h1) { h1_ = h1; }
const String& Security::h1() const     { return h1_; }

bool Security::hasH1() const          { return h1_.length() == 32; }

void Security::setRecoveryOpen(bool b) { recoveryOpen_ = b; }
bool Security::recoveryOpen() const    { return recoveryOpen_; }

bool Security::pair(const String& h1) {
  if (paired_ || hasH1()) return false;   // already paired -> caller 409s
  if (h1.length() != 32) return false;    // MD5 hex sanity
  h1_     = h1;
  paired_ = true;
  // Persistence is the caller's job: WebPortal::handlePair writes
  // S->pairedH1 + saveSettings after pair() returns. Here we just update
  // Security state so the next request sees paired=true.
  return true;
}

bool Security::authorize(ESP8266WebServer& server, bool recovery) const {
  // No H1 yet => setup mode, everything is open. The pair endpoint itself
  // enforces "unpaired only" separately (Task 4).
  (void)recovery;
  if (!hasH1()) return true;
  // Paired: require Digest for every protected route. The ESP8266WebServer
  // API issues the 401 challenge itself if authenticateDigest returns false.
  if (!server.authenticateDigest(SECURITY_USER, h1_.c_str())) {
    server.requestAuthentication(DIGEST_AUTH, SECURITY_REALM);
    return false;
  }
  return true;
}

void Security::serializeIdentity(String& out) const {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["id"]      = deviceId_;
  o["fw"]      = FW_NAME;
  o["version"] = FW_VERSION;
  o["paired"]  = paired_;
  o["mode"]    = (netMode() == NET_AP) ? "ap" : "sta";
  serializeJson(doc, out);
}
