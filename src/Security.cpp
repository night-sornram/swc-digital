#include "Security.h"
#include "Platform.h"     // platformChipId()
#include "config.h"       // FW_NAME, FW_VERSION
#include "Net.h"          // netMode()
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <MD5Builder.h>

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
  // Paired: require HTTP Basic auth. Basic was chosen over Digest because
  // the ESP8266WebServer's Digest impl regenerates the nonce/opaque on every
  // challenge, so browsers (especially Safari) re-prompt every few minutes.
  // Basic lets the browser cache credentials for the whole session.
  //
  // We store only the MD5 H1 (not the plaintext pairkey), so we cannot use
  // server.authenticateBasic(user, plaintext) directly. Instead we decode
  // the incoming Basic header, MD5 the claimed "admin:pairkey", and compare
  // to h1_. Constant-time compare prevents timing leaks.
  if (!server.hasHeader(F("Authorization"))) {
    server.requestAuthentication(BASIC_AUTH, SECURITY_REALM);
    return false;
  }
  String auth = server.header(F("Authorization"));
  if (!auth.startsWith(F("Basic "))) {
    server.requestAuthentication(BASIC_AUTH, SECURITY_REALM);
    return false;
  }
  // Decode base64 after "Basic ".
  String b64 = auth.substring(6);
  b64.trim();
  // mbedtls-style decode via the ESP8266's wrapper. We do a small inline
  // decode (base64 alphabet only) to avoid pulling another lib.
  static const char ALPH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String decoded;
  int val = 0, valb = -8;
  for (size_t i = 0; i < b64.length(); i++) {
    char c = b64[i];
    if (c == '=') break;
    const char* p = strchr(ALPH, c);
    if (!p) continue;
    val = (val << 6) | (p - ALPH);
    valb += 6;
    if (valb >= 0) {
      decoded += (char)((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  // decoded == "admin:pairkey". The stored h1_ is MD5("admin:realm:pairkey")
  // (matches computeH1 in WebPortal.cpp), so rebuild that form before MD5.
  // decoded format: "<user>:<pairkey>"
  int colon = decoded.indexOf(':');
  if (colon < 0) {
    server.requestAuthentication(BASIC_AUTH, SECURITY_REALM);
    return false;
  }
  String user     = decoded.substring(0, colon);
  String pairkey  = decoded.substring(colon + 1);
  String h1_input = user + ":" + SECURITY_REALM + ":" + pairkey;
  MD5Builder md5;
  md5.begin();
  md5.add(h1_input);
  md5.calculate();
  String got = md5.toString();
  // Constant-time compare.
  uint8_t diff = 0;
  for (uint8_t i = 0; i < 32; i++) diff |= (uint8_t)got[i] ^ (uint8_t)h1_[i];
  if (diff != 0) {
    server.requestAuthentication(BASIC_AUTH, SECURITY_REALM);
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
