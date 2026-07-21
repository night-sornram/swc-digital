// swc-digital — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Each feature is a self-contained DisplayMode (see Mode.h), picked in the
// web UI and dispatched from the registry below:
//   - Usage  (features/usage):   Claude/usage display (rewritten in Plan 2).
// Shared plumbing (WiFi, web UI, OTA, display core, settings) lives at src root.
//
// License: WTFPL
#include <Arduino.h>
#include "Platform.h"
#include "config.h"
#include "Settings.h"
#include "Security.h"
#include "Net.h"
#include "Gfx.h"
#include "WebPortal.h"
#include "OtaUpdate.h"
#include "Mode.h"
#include "Clock.h"

#if WITH_USAGE
#include "UsageMode.h"
#include "UsageStore.h"
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_USAGE
  &g_usageMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// ---- AUTO rotation --------------------------------------------------------
// AUTO toggles the usage renderer's active provider every autoRotateSec.
// Manual CODEX/ZAI sticks on the selected provider. Entering AUTO resets timer.
static uint32_t g_autoSwitch = 0;   // millis() of the last provider toggle

static void applyMode(const Settings& s) {
  switch (s.usage.mode) {
    case MODE_CODEX:   g_usageMode.setActiveProvider(PROVIDER_CODEX);   break;
    case MODE_ZAI:     g_usageMode.setActiveProvider(PROVIDER_ZAI);     break;
    case MODE_VITALS:  g_usageMode.setActiveProvider(PROVIDER_VITALS);  break;
    case MODE_WEATHER: g_usageMode.setActiveProvider(PROVIDER_WEATHER); break;
    case MODE_AUTO:
    default: {
      // Start on the first enabled mode in the mask.
      uint8_t mask = s.usage.autoMask;
      if      (mask & 0x01) g_usageMode.setActiveProvider(PROVIDER_CODEX);
      else if (mask & 0x02) g_usageMode.setActiveProvider(PROVIDER_ZAI);
      else if (mask & 0x04) g_usageMode.setActiveProvider(PROVIDER_VITALS);
      else if (mask & 0x08) g_usageMode.setActiveProvider(PROVIDER_WEATHER);
      else                  g_usageMode.setActiveProvider(PROVIDER_CODEX);
      g_autoSwitch = millis();
      break;
    }
  }
}

static Settings g_settings;
static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was an exception -> don't re-enter the crash
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)
#if HAS_LDR
static uint32_t g_lastAutoBr = 0;
static uint8_t  g_ldrCache   = DEFAULT_BRIGHTNESS;   // last LDR reading (2 s cadence)
#endif

// Single brightness resolver: night mode overrides auto-brightness overrides the
// manual level. Only writes the PWM when the effective target changes.
static uint8_t appEffectiveBrightness() {
  if (clockNightActive()) return g_settings.clock.nightLevel;
#if HAS_LDR
  if (g_settings.autoBrightness) {
    if (millis() - g_lastAutoBr > 2000) {
      g_lastAutoBr = millis();
      int raw = analogRead(LDR_PIN);
      g_ldrCache = (uint8_t)constrain(raw * 100 / ADC_MAX, 5, 100);
    }
    return g_ldrCache;
  }
#endif
  return g_settings.brightness;
}

void appApplyBrightness() {
  uint8_t t = appEffectiveBrightness();
  if ((int)t != g_lastBr) {
    g_lastBr = t;
    gfxSetBrightness(t, g_settings.backlightInverted);
  }
}

// Exposed to the web portal (/api/status) so the last reset reason is visible.
const char* appResetReason() { return g_resetReason.c_str(); }

// Called by the web portal after settings are applied: re-init every mode and
// force a fresh repaint so a mode/URL/symbol change takes effect immediately.
void appInvalidate() {
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->invalidate(g_settings);
}

// Called by the web portal after settings are applied: switch the active display
// provider to match the new mode setting (e.g. vitals, weather). Without this,
// changing mode via the WebUI only takes effect after a reboot.
void appApplyMode() { applyMode(g_settings); }

static void bootProgress(const char* msg) {
  gfxBoot("SmallTV", msg);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  // Capture why we (re)booted. On a reboot loop this is the key clue, and the
  // device's UART isn't exposed — so we also show it on screen below. On the
  // ESP8266 we also keep the crash PC (epc1) for addr2line decoding; the
  // ESP32-C2 (RISC-V) doesn't expose it, so epc/addr come back empty there.
  PlatformReset pr = platformResetInfo();
  Serial.print("[boot] reset reason: ");
  Serial.println(pr.reason);

  if (pr.wasCrash) {
    g_safeMode = true;                   // crashed last boot -> stay out of the crash path
    strlcpy(g_epcStr,  pr.epc,  sizeof(g_epcStr));
    strlcpy(g_addrStr, pr.addr, sizeof(g_addrStr));
    char rich[80];
    snprintf(rich, sizeof(rich), "%s epc %s addr %s", pr.reason.c_str(),
             g_epcStr[0] ? g_epcStr : "-", g_addrStr[0] ? g_addrStr : "-");
    g_resetReason = rich;
  } else {
    g_resetReason = pr.reason;
  }

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);

  Serial.println("[boot] security");
  g_security.begin();

  Serial.println("[boot] display");
  gfxBegin(g_settings);
  gfxBoot(g_safeMode ? "Crashed" : "SmallTV", FW_VERSION);

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that WiFi (STA) is up — but only if night mode is enabled, so a
  // ticker-only device doesn't pay the SNTP heap cost (which can starve the cash.ch
  // TLS handshake on the ESP8266). clockReapply arms it iff needed. Skipped after a
  // crash so a fault in here can't boot-loop before the web server starts (the
  // device then comes up in safe mode, OTA-recoverable, instead of needing UART).
  if (!g_safeMode) clockReapply(g_settings);

  // A GitHub update queued from the web UI runs now, before the features claim
  // the heap (the download needs a 16 KB TLS buffer that only fits at boot).
  // On success it reboots into the new image; a no-op stub on the ESP32 targets.
  if (otaBootRequested()) {
    Serial.println("[boot] github update");
    gfxBoot("SmallTV", "updating...");
    otaBootUpdate(g_settings);
    gfxBoot("SmallTV", "update failed");   // still here -> failed; details in the web UI
    delay(1200);
  }

  Serial.println("[boot] web");
  webPortalBegin(g_settings);

  Serial.println("[boot] modes");
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);
  applyMode(g_settings);
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str());
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
    delay(3500);
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  // Serial recovery: type "FACTORY RESET" over USB to clear pairing and
  // reboot unpaired. Last-resort escape hatch when a firmware auth bug
  // locks out WebUI (the v3.1.2 incident). The CH340 bridge is always
  // present on SmallTV-ultra hardware, so this works even when Wi-Fi auth
  // is broken — only USB + a terminal is needed.
  if (Serial.available() >= 13) {
    static char buf[32];
    static uint8_t n = 0;
    while (Serial.available() && n < sizeof(buf) - 1) {
      buf[n++] = Serial.read();
    }
    buf[n] = 0;
    // Slide window looking for the command (case-insensitive).
    String s = String(buf);
    s.toUpperCase();
    if (s.indexOf("FACTORY RESET") >= 0) {
      Serial.println(F("[recovery] clearing pairing + rebooting"));
      g_settings.pairedH1 = "";
      saveSettings(g_settings);
      g_security.clearPairing();
      delay(200);
      ESP.restart();
    }
    n = 0;  // reset buffer each loop iteration that found input
  }

  // Auto-unpair: if authorize() saw N consecutive failed Basic auth attempts
  // (firmware bug / wrong key streak), clear pairing and reboot. Persists
  // so the device boots unpaired the next time.
  if (g_security.autoUnpairTriggered()) {
    Serial.println(F("[recovery] auto-unpair triggered by auth-fail streak"));
    g_settings.pairedH1 = "";
    saveSettings(g_settings);
    g_security.clearAutoUnpairFlag();
    delay(200);
    ESP.restart();
  }

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  if (g_safeMode) {
    delay(5);
    return;  // crashed last boot: web UI stays up for OTA recovery, no rendering
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: the active feature fetches + renders itself ---

  // Night-mode state machine (NTP-trust gate), then apply the effective brightness
  // (night override / auto-brightness / manual level).
  clockService(g_settings);
  appApplyBrightness();

  // AUTO rotation: single dwell time for all enabled providers.
  if (g_settings.usage.mode == MODE_AUTO) {
    if (g_autoSwitch == 0) g_autoSwitch = millis();
    uint16_t dwell = g_settings.usage.autoRotateSec;
    if (millis() - g_autoSwitch >= (uint32_t)dwell * 1000UL) {
      g_autoSwitch = millis();
      g_usageMode.toggleAutoProvider(g_settings.usage.autoMask);
    }
  }

  // Single-mode registry: find &g_usageMode and dispatch service().
  DisplayMode* m = nullptr;
  for (size_t i = 0; i < kModeCount; i++) {
    if (kModes[i] == &g_usageMode) { m = kModes[i]; break; }
  }
  if (m) m->service(g_settings);

  delay(5);
}
