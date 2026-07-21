// UsageApi.h — HTTP push/read handlers for /api/usage.
//
// The Mac usage service POSTs one body per provider here. We validate strictly
// (per UsageStore) and never echo credentials. The device stores NO provider
// token: it only holds the most recent pushed snapshot.
#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>

// Register handlers on the given server (called from webPortalBegin). The
// server instance lives in WebPortal.cpp; we expose begin() so UsageApi does
// not need its own server. ESP8266WebServer's real type is a template alias
// (esp8266webserver::ESP8266WebServerTemplate<WiFiServer>) which cannot be
// forward-declared, so we pull in the framework header here. WebPortal.cpp
// gets the same alias via Platform.h's `using WebServerClass`.
void usageApiBegin(ESP8266WebServer& server);
