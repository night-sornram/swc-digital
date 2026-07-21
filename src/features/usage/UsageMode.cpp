#include "UsageMode.h"
#include "UsageStore.h"
#include "../../config.h"
#include "../../Gfx.h"
#include <Arduino_GFX_Library.h>   // full Arduino_GFX definition (Gfx.h only fwd-declares it)

UsageMode g_usageMode;

// NOTE on text width: this GFX Library for Arduino version (moononournation
// @ ^1.5.0) does NOT expose Arduino_GFX::textWidth(). The plan anticipated this
// and authorised the gfxTextW() fallback (Gfx.h, built-in 6x8 font scaled by
// `size`). The built-in font is exactly what setTextSize() selects (no setFont
// call is made below), so gfxTextW(s, size) matches the device rendering.
static uint16_t barColorFor(uint8_t pct, uint16_t providerColor, bool stale) {
  if (stale) return USAGE_COLOR_STALE;
  if (pct >= 90) return USAGE_COLOR_CRIT;
  if (pct >= 70) return USAGE_COLOR_WARN;
  return providerColor;
}

static void drawCard(int16_t y, const char* label, const UsageWindow& w,
                     uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  // Card background.
  d->fillRoundRect(8, y, 224, 74, 6, USAGE_COLOR_CARD);
  // Label (top-left of card).
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(18, y + 8);
  d->print(label);
  // Big percentage (right side) or N/A.
  d->setTextSize(4);
  if (w.available) {
    d->setTextColor(barColorFor(w.usedPct, providerColor, stale));
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", w.usedPct);
    int16_t tw = gfxTextW(buf, 4);   // fallback: textWidth() unavailable in this GFX version
    d->setCursor(222 - tw, y + 10);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "N/A";
    int16_t tw = gfxTextW(na, 4);    // fallback: textWidth() unavailable in this GFX version
    d->setCursor(222 - tw, y + 10);
    d->print(na);
  }
  // Progress bar (bottom of card).
  const int16_t by = y + 50, bh = 10, bx = 18, bw = 204;
  d->fillRoundRect(bx, by, bw, bh, 4, USAGE_COLOR_BG);
  if (w.available && w.usedPct > 0) {
    int16_t fw = (int16_t)(bw * (uint32_t)w.usedPct / 100UL);
    if (fw < 4) fw = 4;
    d->fillRoundRect(bx, by, fw, bh, 4, barColorFor(w.usedPct, providerColor, stale));
  }
  // Reset countdown (small, under the bar).
  d->setTextSize(1);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(18, by + 14);
  if (w.available && w.resetMin != 0xFFFF) {
    char buf[24];
    snprintf(buf, sizeof(buf), "RESET %um", w.resetMin);
    d->print(buf);
  } else {
    d->print("RESET --");
  }
}

void UsageMode::begin(const Settings& s) {
  needsFullRedraw_ = true;
  // Default the active provider from settings.mode: CODEX/ZAI pick directly,
  // AUTO starts on CODEX.
  active_ = (s.mode == MODE_ZAI) ? PROVIDER_ZAI : PROVIDER_CODEX;
}

void UsageMode::invalidate(const Settings& s) {
  begin(s);
}

void UsageMode::wake(const Settings& s) {
  // Re-entering from another screen: just repaint, do not refetch (there is no fetch).
  needsFullRedraw_ = true;
}

void UsageMode::setActiveProvider(UsageProvider p) {
  if (p == active_) return;
  active_ = p;
  needsFullRedraw_ = true;
}

void UsageMode::toggleAutoProvider() {
  active_ = (active_ == PROVIDER_CODEX) ? PROVIDER_ZAI : PROVIDER_CODEX;
  needsFullRedraw_ = true;
}

void UsageMode::service(const Settings& s) {
  const ProviderUsage& pu = g_usageStore.read(active_);
  bool stale = g_usageStore.stale(active_);
  uint16_t providerColor = usageProviderColor(active_);

  // Full redraw path.
  if (needsFullRedraw_) {
    needsFullRedraw_ = false;
    auto* d = gfxDev();
    d->fillScreen(USAGE_COLOR_BG);
    // Title bar.
    d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
    d->setTextColor(providerColor);
    d->setTextSize(3);
    d->setCursor(10, 8);
    d->print(usageProviderLabel(active_));
    // LIVE / STALE pill (right).
    d->setTextSize(2);
    const char* pill = stale ? "STALE" : "LIVE";
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    int16_t tw = gfxTextW(pill, 2);   // fallback: textWidth() unavailable in this GFX version
    d->setCursor(232 - tw, 10);
    d->print(pill);
    // Cards.
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    // Reset dirty trackers so subsequent partial updates redraw correctly.
    for (uint8_t i = 0; i < PROVIDER_COUNT; i++) {
      lastFiveHourOk_[i]  = g_usageStore.read((UsageProvider)i).fiveHour.available
                            ? g_usageStore.read((UsageProvider)i).lastOkMs : 0;
      lastWeeklyOk_[i]    = g_usageStore.read((UsageProvider)i).weekly.available
                            ? g_usageStore.read((UsageProvider)i).lastOkMs : 0;
    }
    lastFiveHourReset_[active_] = pu.fiveHour.resetMin;
    lastWeeklyReset_[active_]   = pu.weekly.resetMin;
    lastAgeMin_[active_]        = 0xFFFF;
    lastStale_[active_]         = stale;
    return;
  }

  // Partial: status + cards only if lastOkMs changed or stale flipped.
  bool dataChanged = (pu.lastOkMs != lastFiveHourOk_[active_]) || (stale != lastStale_[active_]);
  if (dataChanged) {
    auto* d = gfxDev();
    // Repaint just the pill + cards region (y=8..198) over a fresh card bg.
    // (Cheaper than a full clear; never touches y=204..239 to avoid age flicker.)
    d->fillRect(0, 8, 240, 27, USAGE_COLOR_CARD);   // title bar minus label area
    // Re-draw label so the cleared bar is not blank.
    d->setTextColor(providerColor);
    d->setTextSize(3);
    d->setCursor(10, 8);
    d->print(usageProviderLabel(active_));
    d->setTextSize(2);
    const char* pill = stale ? "STALE" : "LIVE";
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    int16_t tw = gfxTextW(pill, 2);   // fallback: textWidth() unavailable in this GFX version
    d->setCursor(232 - tw, 10);
    d->print(pill);
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    lastFiveHourOk_[active_] = pu.lastOkMs;
    lastWeeklyOk_[active_]   = pu.lastOkMs;
    lastStale_[active_]      = stale;
  }

  // Partial: reset countdown only when the minute value changed.
  // (Codex/z.ai push reset_min as minutes already; we redraw the card's reset
  // row when it changed since last paint. Cheap: just compare to last value.)
  if (pu.fiveHour.resetMin != lastFiveHourReset_[active_] ||
      pu.weekly.resetMin   != lastWeeklyReset_[active_]) {
    // Redrawing the whole card is simplest given the small text region overlap;
    // it is bounded (74px tall) and only runs on a real change.
    drawCard(42,  "5H",     pu.fiveHour, providerColor, stale);
    drawCard(124, "WEEKLY", pu.weekly,   providerColor, stale);
    lastFiveHourReset_[active_] = pu.fiveHour.resetMin;
    lastWeeklyReset_[active_]   = pu.weekly.resetMin;
  }

  // Partial: age row (y=204..239) only when the minute value changed.
  uint32_t age = g_usageStore.ageMs(active_);
  uint16_t ageMin = (age == 0xFFFFFFFFUL) ? 0xFFFF : (uint16_t)(age / 60000UL);
  if (ageMin != lastAgeMin_[active_]) {
    auto* d = gfxDev();
    d->fillRect(0, 204, 240, 36, USAGE_COLOR_BG);
    d->setTextSize(2);
    d->setTextColor(USAGE_COLOR_MUTED);
    d->setCursor(10, 212);
    if (age == 0xFFFFFFFFUL) {
      d->print("AGE --");
    } else {
      char buf[20];
      snprintf(buf, sizeof(buf), "AGE %um", ageMin);
      d->print(buf);
    }
    // AUTO / MANUAL marker (right).
    const char* am = (s.mode == MODE_AUTO) ? "AUTO" : "MANUAL";
    d->setTextColor((s.mode == MODE_AUTO) ? providerColor : USAGE_COLOR_MUTED);
    int16_t tw = gfxTextW(am, 2);   // fallback: textWidth() unavailable in this GFX version
    d->setCursor(232 - tw, 212);
    d->print(am);
    lastAgeMin_[active_] = ageMin;
  }
}
