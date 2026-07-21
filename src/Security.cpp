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
  // paired_ and h1_ are filled later by Plan 2 (setPaired/setH1).
  paired_ = false;
  h1_     = "";
}

const char* Security::deviceId() const { return deviceId_; }

bool Security::paired() const         { return paired_; }
void Security::setPaired(bool p)      { paired_ = p; }

void Security::setH1(const String& h1) { h1_ = h1; }
const String& Security::h1() const     { return h1_; }

bool Security::authorize(ESP8266WebServer& server, bool recovery) const {
  // Plan 1: seam only — everything is allowed. Plan 2 turns on Digest
  // verification for paired+STA mode, keeping the recovery escape hatch.
  (void)server; (void)recovery;
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
