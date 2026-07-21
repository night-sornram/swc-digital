// Security.h — central seam for Device ID, pairing state, and route auth.
//
// One singleton (g_security) holds the device identity and (in later plans)
// the Digest H1 used to authorize every protected route. In Plan 1 it only
// exposes the identity + the seam; Plan 2 fills in the Digest verification
// and the lock-down of every route.
#pragma once
#include <Arduino.h>
// NOTE: WebServerClass is a template alias (esp8266webserver::ESP8266WebServerTemplate<WiFiServer>),
// NOT a plain class — so it cannot be forward-declared. We must include the
// header. Platform.h re-exposes the same type via `using WebServerClass`.
#include <ESP8266WebServer.h>

class Security {
 public:
  void begin();

  // Stable per-device identity (ESP.getChipId(), hex, 8 chars). Does NOT
  // change with hostname, IP, or WiFi network.
  const char* deviceId() const;

  // Pairing state. Unpaired in Plan 1 always (Plan 2 fills setPaired()).
  bool paired() const;
  void setPaired(bool p);

  // Digest H1 (MD5 of "admin:<realm>:<pairkey>", lowercase hex). Empty when
  // unpaired. Plan 2 sets it from the pair flow; Plan 1 stores it but does
  // not verify.
  void setH1(const String& h1);
  const String& h1() const;

  // True iff the device has a valid H1 stored (32-hex). Read-only convenience
  // used by handlers to decide whether the device is in setup mode.
  bool hasH1() const;

  // Pair the device: store H1, mark paired. Returns false if already paired
  // (caller 409s) or if h1 is not a 32-hex string. Persistence is the caller's
  // job (WebPortal::handlePair writes Settings); this updates Security state
  // only.
  bool pair(const String& h1);

  // AP-recovery escape hatch flag. When true, AP-mode handlers may relax
  // routing (Plan 5 toggles this on entering AP setup mode). Does not bypass
  // Digest on protected routes once paired.
  void setRecoveryOpen(bool b);
  bool recoveryOpen() const;

  // Authorize the current request. Returns true if allowed.
  //   recovery = true  => AP recovery mode: pair flow runs unpaired, every
  //                       other route still requires Digest once paired.
  //   recovery = false => STA mode: every protected route requires Digest.
  // Unpaired devices (no H1) allow everything (they are in setup).
  // Uses ESP8266WebServer (the concrete type) so this header stays decoupled
  // from Platform.h; Platform.h's `using WebServerClass = ESP8266WebServer`
  // means callers can pass either name.
  bool authorize(ESP8266WebServer& server, bool recovery) const;

  // Build the /api/identity JSON body (no secrets, no credentials).
  void serializeIdentity(String& out) const;

 private:
  char   deviceId_[9] = {0};   // 8 hex chars + NUL
  bool   paired_      = false;
  String h1_;                  // MD5 hex, or empty
  bool   recoveryOpen_ = false;
};

extern Security g_security;

// Realm used by Digest auth. Constant so the Mac can compute the same H1.
// Defined here (not in config.h) to keep the Security module self-contained.
extern const char* SECURITY_REALM;
extern const char* SECURITY_USER;   // "admin"
