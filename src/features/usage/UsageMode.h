// UsageMode.h — placeholder. Plan 2 rewrites this.
#pragma once
#include "Mode.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override        { return "usage"; }
  uint8_t     modeConst() const override { return MODE_USAGE; }
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
 private:
  bool needRender_ = true;
};

extern UsageMode g_usageMode;
