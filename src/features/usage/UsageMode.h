// UsageMode.h — the usage display renderer (CODEX / Z.AI / AUTO).
//
// One renderer, parameterised by provider. In AUTO the active provider is
// toggled by main.cpp on the autoRotateSec timer. Redraw rules:
//   - full redraw: on mode change, provider change, rotation change, visual cfg
//   - status+cards: on new data (lastOkMs changed)
//   - countdown + age: only when their minute value changes
// Never clear/redraw the full panel every second.
#pragma once
#include "Mode.h"
#include "UsageStore.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override        { return "usage"; }
  uint8_t     modeConst() const override { return MODE_AUTO; }  // see note below
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

  // Called by main.cpp when AUTO rotates: repaint the new provider.
  void setActiveProvider(UsageProvider p);
  void toggleAutoProvider(uint8_t mask);   // rotate to next enabled mode in mask
  UsageProvider activeProvider() const { return active_; }   // for per-provider dwell
 private:
  bool needsFullRedraw_ = true;
  UsageProvider active_ = PROVIDER_CODEX;
  // Last values rendered (dirty tracking).
  uint32_t lastFiveHourOk_[PROVIDER_COUNT]  = {0};
  uint32_t lastWeeklyOk_[PROVIDER_COUNT]    = {0};
  uint16_t lastFiveHourReset_[PROVIDER_COUNT] = {0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t lastWeeklyReset_[PROVIDER_COUNT]   = {0xFFFF, 0xFFFF, 0xFFFF};
  Freshness lastFresh_[PROVIDER_COUNT]        = {Freshness::LIVE, Freshness::LIVE, Freshness::LIVE};
  // WEATHER-only dirty trackers (clock + calendar are device-local, not
  // pushed; they change on their own cadence).
  uint8_t  lastClockMin_  = 0xFF;   // last minute rendered (0..59)
  uint8_t  lastClockDay_  = 0xFF;   // last day-of-month rendered (1..31)
  bool     weatherFirstDraw_ = true;
};

extern UsageMode g_usageMode;
