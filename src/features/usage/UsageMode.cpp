// UsageMode.cpp — placeholder.
//
// 3.0.0 replaces this with the CODEX/ZAI/AUTO usage display (Plan 2). This
// skeleton keeps the DisplayMode interface alive so main.cpp still compiles
// after Ticker/Radar/mascot removal.
#include "UsageMode.h"
#include "UsageClient.h"

UsageMode g_usageMode;

void UsageMode::begin(const Settings& s)   { needRender_ = true; usageInit(s); }
void UsageMode::service(const Settings& s) { usageService(s); }
void UsageMode::invalidate(const Settings& s) { needRender_ = true; }
void UsageMode::wake(const Settings& s)    { needRender_ = true; }
