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

  // Authorize the current request. Returns true if allowed. In Plan 1 this
  // is ALWAYS true (no auth yet) — the seam exists so Plan 2 can flip it to
  // "Digest-verify against h1, exempt only in AP recovery mode".
  // `recovery` = true is the AP-recovery escape hatch (Plan 2 fills it).
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
};

extern Security g_security;

// Realm used by Digest auth. Constant so the Mac can compute the same H1.
// Defined here (not in config.h) to keep the Security module self-contained.
extern const char* SECURITY_REALM;
extern const char* SECURITY_USER;   // "admin"
