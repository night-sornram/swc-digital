#include "UsageApi.h"
#include "UsageStore.h"
#include "../../Platform.h"   // WebServerClass alias (matches ESP8266WebServer)
#include "../../Security.h"
#include "../../Net.h"
#include <ArduinoJson.h>

// File-scope pointer to the server (set once in usageApiBegin). The route
// handlers below are free functions so they do not depend on lambda capture
// lifetime, which is fragile with ESP8266WebServer's THandlerFunction copy.
static ESP8266WebServer* s_server = nullptr;

static void sendUsageOverview() {
  if (!g_security.authorize(*s_server, netMode() == NET_AP)) return;
  String out;
  g_usageStore.serializeOverview(out);
  s_server->send(200, "application/json", out);
}

static void handleUsagePost() {
  if (!g_security.authorize(*s_server, netMode() == NET_AP)) return;
  if (!s_server->hasArg("plain")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
    return;
  }
  const String& body = s_server->arg("plain");
  // Peek the provider token to route the push to the right store slot.
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  const char* tok = doc["provider"] | "";
  UsageProvider p;
  if (strcasecmp(tok, "codex") == 0)         p = PROVIDER_CODEX;
  else if (strcasecmp(tok, "zai") == 0)      p = PROVIDER_ZAI;
  else if (strcasecmp(tok, "system") == 0)   p = PROVIDER_SYSTEM;
  else {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad provider\"}");
    return;
  }
  // applyPush validates strictly; last-good snapshot is untouched on failure.
  bool ok = g_usageStore.applyPush(p, body);
  s_server->send(ok ? 200 : 400, "application/json",
                 ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid\"}");
}

void usageApiBegin(ESP8266WebServer& server) {
  s_server = &server;
  server.on("/api/usage", HTTP_GET,  sendUsageOverview);
  server.on("/api/usage", HTTP_POST, handleUsagePost);
}
