#include "UsageMode.h"
#include "UsageStore.h"
#include "../../config.h"
#include "../../Gfx.h"
#include "../../Clock.h"            // clockNow() / clockSynced() for the WEATHER branch
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

// WMO weather code → label + icon drawing.
// See open-meteo docs: 0=clear, 1-3=cloudy, 45-48=fog, 51-67=rain,
// 71-77=snow, 80-82=showers, 95-99=thunderstorm.

// Draw a simple weather icon at (cx, cy) center. Size ~20px.
// Uses GFX primitives since the built-in font has no icon glyphs.
static void drawWeatherIcon(int16_t cx, int16_t cy, uint8_t code,
                            uint16_t providerColor) {
  auto* d = gfxDev();
  if (code == 0) {
    // Sun: filled circle + outer ring.
    d->fillCircle(cx, cy, 8, 0xFD84);    // yellow #FFB020
    d->drawCircle(cx, cy, 12, 0xFD84);
    // Rays (4 short lines).
    d->drawFastVLine(cx, cy - 14, 4, 0xFD84);
    d->drawFastVLine(cx, cy + 11, 4, 0xFD84);
    d->drawFastHLine(cx - 14, cy, 4, 0xFD84);
    d->drawFastHLine(cx + 11, cy, 4, 0xFD84);
  } else if (code >= 1 && code <= 3) {
    // Cloud: two overlapping rounded rects.
    d->fillRoundRect(cx - 12, cy - 2, 26, 14, 6, 0x8D16);  // muted gray
    d->fillRoundRect(cx - 4, cy - 10, 18, 14, 6, 0x8D16);
  } else if (code >= 45 && code <= 48) {
    // Fog: horizontal lines.
    for (int i = -6; i <= 6; i += 4)
      d->drawFastHLine(cx - 12, cy + i, 24, 0x8D16);
  } else if (code >= 51 && code <= 67 || code >= 80 && code <= 82) {
    // Rain: cloud + drops.
    d->fillRoundRect(cx - 10, cy - 8, 22, 12, 5, 0x8D16);
    d->fillRoundRect(cx - 2, cy - 14, 14, 12, 5, 0x8D16);
    // 3 rain drops.
    d->drawFastVLine(cx - 6, cy + 6, 6, 0x03FF);  // blue
    d->drawFastVLine(cx,     cy + 8, 6, 0x03FF);
    d->drawFastVLine(cx + 6, cy + 6, 6, 0x03FF);
  } else if (code >= 71 && code <= 77) {
    // Snow: cloud + dots.
    d->fillRoundRect(cx - 10, cy - 8, 22, 12, 5, 0x8D16);
    d->fillRoundRect(cx - 2, cy - 14, 14, 12, 5, 0x8D16);
    d->drawPixel(cx - 5, cy + 6, 0xFFFF);
    d->drawPixel(cx,     cy + 8, 0xFFFF);
    d->drawPixel(cx + 5, cy + 6, 0xFFFF);
  } else {
    // Storm / default: cloud + lightning (zigzag).
    d->fillRoundRect(cx - 10, cy - 8, 22, 12, 5, 0x8D16);
    d->fillRoundRect(cx - 2, cy - 14, 14, 12, 5, 0x8D16);
    d->drawLine(cx - 2, cy + 4, cx + 2, cy + 8, 0xFD84);   // yellow
    d->drawLine(cx + 2, cy + 8, cx - 1, cy + 10, 0xFD84);
  }
}

static const char* wmoLabel(uint8_t code) {
  if (code == 0)                       return "Clear";
  if (code >= 1 && code <= 3)          return "Cloudy";
  if (code >= 45 && code <= 48)        return "Fog";
  if (code >= 51 && code <= 67)        return "Rain";
  if (code >= 71 && code <= 77)        return "Snow";
  if (code >= 80 && code <= 82)        return "Showers";
  if (code >= 95)                      return "Storm";
  return "---";
}

// AQI color by european_aqi band.
static uint16_t aqiColor(uint8_t eaqi) {
  if (eaqi < 20)  return 0x150F;   // green #10A37F
  if (eaqi < 40)  return 0xBDFD;   // teal #36D6C4
  if (eaqi < 60)  return 0xFD84;   // amber #FFB020
  if (eaqi < 80)  return 0xFC80;   // orange
  return            0xF28A;        // red #F05252
}

static void drawCard(int16_t y, const char* label, const UsageWindow& w,
                     uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  // Card background.
  d->fillRoundRect(4, y, 232, 74, 6, USAGE_COLOR_CARD);
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
    d->setCursor(226 - tw, y + 10);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "N/A";
    int16_t tw = gfxTextW(na, 4);    // fallback: textWidth() unavailable in this GFX version
    d->setCursor(226 - tw, y + 10);
    d->print(na);
  }
  // Progress bar (bottom of card).
  const int16_t by = y + 42, bh = 8, bx = 14, bw = 212;
  d->fillRoundRect(bx, by, bw, bh, 3, USAGE_COLOR_BG);
  if (w.available && w.usedPct > 0) {
    int16_t fw = (int16_t)(bw * (uint32_t)w.usedPct / 100UL);
    if (fw < 4) fw = 4;
    d->fillRoundRect(bx, by, fw, bh, 3, barColorFor(w.usedPct, providerColor, stale));
  }
  // Reset countdown (under the bar, readable size 2).
  d->setTextSize(2);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(18, by + 12);
  if (w.available && w.resetMin != 0xFFFF) {
    char buf[24];
    // Compact d/h/m: 5711m -> "3d 23h", 95m -> "1h 35m", 5m -> "5m".
    uint16_t m = w.resetMin;
    uint16_t days  = m / 1440; m -= days * 1440;
    uint16_t hours = m / 60;   m -= hours * 60;
    if (days)        snprintf(buf, sizeof(buf), "%ud %uh %um", days, hours, m);
    else if (hours)  snprintf(buf, sizeof(buf), "%uh %um",     hours, m);
    else             snprintf(buf, sizeof(buf), "%um", m);
    d->print(buf);
  } else {
    d->print("--");
  }
}

// SYSTEM-only: compact 3-card layout for CPU/RAM/SSD. Each card is shorter
// (50 px tall) so three fit between the title bar (y=0..35) and the age row
// (y=204..239). No reset countdown — system metrics don't have a "reset".
static void drawSystemCard(int16_t y, const char* label, uint8_t pct, bool avail,
                           uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  d->fillRoundRect(4, y, 232, 50, 5, USAGE_COLOR_CARD);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(18, y + 6);
  d->print(label);
  d->setTextSize(3);
  if (avail) {
    d->setTextColor(barColorFor(pct, providerColor, stale));
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", pct);
    int16_t tw = gfxTextW(buf, 3);
    d->setCursor(226 - tw, y + 6);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "N/A";
    int16_t tw = gfxTextW(na, 3);
    d->setCursor(226 - tw, y + 6);
    d->print(na);
  }
  // Slim bar.
  const int16_t by = y + 34, bh = 8, bx = 14, bw = 212;
  d->fillRoundRect(bx, by, bw, bh, 3, USAGE_COLOR_BG);
  if (avail && pct > 0) {
    int16_t fw = (int16_t)(bw * (uint32_t)pct / 100UL);
    if (fw < 3) fw = 3;
    d->fillRoundRect(bx, by, fw, bh, 3, barColorFor(pct, providerColor, stale));
  }
}

// VITALS half-width card (108 wide) for the 2×3 grid. Same visual language
// as drawSystemCard but compact: label top-left, big % top-right, slim bar.
// For temp metrics, isTemp=true prints a degree symbol instead of % and no bar.
static void drawVitalsCard(int16_t x, int16_t y, const char* label,
                           uint8_t pct, bool avail, bool isTemp,
                           uint16_t providerColor, bool stale) {
  auto* d = gfxDev();
  d->fillRoundRect(x, y, 114, 70, 5, USAGE_COLOR_CARD);
  // Label (top).
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(x + 8, y + 4);
  d->print(label);
  // Big number (below label, right-aligned).
  d->setTextSize(3);
  if (avail) {
    d->setTextColor(barColorFor(pct, providerColor, stale));
    char buf[10];
    if (isTemp) {
      snprintf(buf, sizeof(buf), "%dc", (int)(int8_t)pct);
    } else {
      snprintf(buf, sizeof(buf), "%u%%", pct);
    }
    int16_t tw = gfxTextW(buf, 3);
    d->setCursor(x + 106 - tw, y + 22);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "--";
    int16_t tw = gfxTextW(na, 3);
    d->setCursor(x + 106 - tw, y + 22);
    d->print(na);
  }
  // Slim bar (only for % metrics, not temp).
  if (!isTemp) {
    const int16_t by = y + 54, bh = 6, bx = x + 8, bw = 98;
    d->fillRoundRect(bx, by, bw, bh, 3, USAGE_COLOR_BG);
    if (avail && pct > 0) {
      int16_t fw = (int16_t)(bw * (uint32_t)pct / 100UL);
      if (fw < 3) fw = 3;
      d->fillRoundRect(bx, by, fw, bh, 3, barColorFor(pct, providerColor, stale));
    }
  }
}

// VITALS-only: full-width banner with battery + uptime. Two cells side by
// side in one 224-wide card, h=30.
static void drawVitalsBanner(int16_t y, uint8_t batteryPct, uint16_t uptimeMin,
                             bool stale) {
  auto* d = gfxDev();
  d->fillRoundRect(4, y, 232, 30, 5, USAGE_COLOR_CARD);
  // Battery cell (left).
  d->setTextSize(2);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(14, y + 8);
  d->print("BAT");
  if (batteryPct != 0xFF) {
    d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", batteryPct);
    d->setCursor(54, y + 8);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    d->setCursor(54, y + 8);
    d->print("--");
  }
  // Uptime cell (right). Compact d/h/m.
  d->setTextSize(2);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setCursor(124, y + 8);
  d->print("UP");
  d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
  if (uptimeMin != 0xFFFF) {
    char buf[16];
    uint16_t m = uptimeMin;
    uint16_t days = m / 1440; m -= days * 1440;
    uint16_t hrs  = m / 60;   m -= hrs * 60;
    if (days)        snprintf(buf, sizeof(buf), "%ud %uh", days, hrs);
    else if (hrs)    snprintf(buf, sizeof(buf), "%uh %um", hrs, m);
    else             snprintf(buf, sizeof(buf), "%um", m);
    d->setCursor(162, y + 8);
    d->print(buf);
  } else {
    d->setCursor(162, y + 8);
    d->print("--");
  }
}

void UsageMode::begin(const Settings& s) {
  needsFullRedraw_ = true;
  // Default the active provider from settings.mode: CODEX/ZAI/SYSTEM pick
  // directly, AUTO starts on CODEX.
  if (s.mode == MODE_ZAI)         active_ = PROVIDER_ZAI;
  else if (s.mode == MODE_SYSTEM) active_ = PROVIDER_SYSTEM;
  else                            active_ = PROVIDER_CODEX;
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

void UsageMode::toggleAutoProvider(uint8_t mask) {
  // Rotation order: codex(0) → zai(1) → vitals(2) → weather(3) → codex(0).
  // Each mode is enabled if its bit is set in mask.
  // Find the next enabled mode after the current one.
  static const uint8_t ORDER[] = {0, 1, 2, 3};  // codex, zai, vitals, weather
  static const UsageProvider PROVS[] = {PROVIDER_CODEX, PROVIDER_ZAI,
                                        PROVIDER_VITALS, PROVIDER_WEATHER};
  // Map current active to index in ORDER.
  uint8_t cur = 0;
  switch (active_) {
    case PROVIDER_ZAI:     cur = 1; break;
    case PROVIDER_VITALS:  cur = 2; break;
    case PROVIDER_WEATHER: cur = 3; break;
    default:               cur = 0; break;
  }
  // Find next enabled (wrapping).
  for (uint8_t i = 1; i <= 4; i++) {
    uint8_t idx = (cur + i) % 4;
    if (mask & (1 << ORDER[idx])) {
      active_ = PROVS[idx];
      needsFullRedraw_ = true;
      return;
    }
  }
  // Fallback: stay on current.
}

void UsageMode::service(const Settings& s) {
  const ProviderUsage& pu = g_usageStore.read(active_);
  bool stale = g_usageStore.stale(active_);
  uint16_t providerColor = usageProviderColor(active_);

  // SYSTEM provider: dedicated 3-card compact layout. Keeps the same
  // dirty-tracking model (full redraw only on enter / data change).
  if (active_ == PROVIDER_SYSTEM) {
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
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // 3 compact cards: CPU / RAM / SSD. five_hour=CPU, weekly=RAM, extra=SSD.
      drawSystemCard(42,  "CPU", pu.fiveHour.usedPct, pu.fiveHour.available, providerColor, stale);
      drawSystemCard(96,  "RAM", pu.weekly.usedPct,   pu.weekly.available,   providerColor, stale);
      bool ssd_avail = (pu.extraPct != 0xFF);
      drawSystemCard(150, "DISK", pu.extraPct,        ssd_avail,             providerColor, stale);
      lastStale_[active_]         = stale;
      lastFiveHourOk_[active_]    = pu.lastOkMs;
      lastWeeklyOk_[active_]      = pu.lastOkMs;
      return;
    }
    // Partial: data changed → full repaint of card region (3 cards cheap).
    if (pu.lastOkMs != lastFiveHourOk_[active_] || stale != lastStale_[active_]) {
      drawSystemCard(42,  "CPU", pu.fiveHour.usedPct, pu.fiveHour.available, providerColor, stale);
      drawSystemCard(96,  "RAM", pu.weekly.usedPct,   pu.weekly.available,   providerColor, stale);
      bool ssd_avail = (pu.extraPct != 0xFF);
      drawSystemCard(150, "DISK", pu.extraPct,        ssd_avail,             providerColor, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastWeeklyOk_[active_]   = pu.lastOkMs;
      lastStale_[active_]      = stale;
    }
    // Age row (same as AI providers).
    return;   // fall through below is the AI-provider path; SYSTEM returns here.
  }

  // ---- VITALS: Grid 2×3 (CPU/RAM/SSD/TEMP cards + BAT/UP banner) ----
  if (active_ == PROVIDER_VITALS) {
    if (needsFullRedraw_) {
      needsFullRedraw_ = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      d->print("MAC");
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // CPU hero card (full width, VITALS style: label + big number).
      auto* d2 = gfxDev();
      d2->fillRoundRect(4, 42, 232, 56, 5, USAGE_COLOR_CARD);
      d2->setTextColor(USAGE_COLOR_MUTED);
      d2->setTextSize(2);
      d2->setCursor(18, 48);
      d2->print("CPU");
      d2->setTextSize(4);
      if (pu.fiveHour.available) {
        d2->setTextColor(barColorFor(pu.fiveHour.usedPct, providerColor, stale));
        char buf[8]; snprintf(buf, sizeof(buf), "%u%%", pu.fiveHour.usedPct);
        int16_t tw2 = gfxTextW(buf, 4);
        d2->setCursor(226 - tw2, 50);
        d2->print(buf);
      } else {
        d2->setTextColor(USAGE_COLOR_MUTED);
        int16_t tw2 = gfxTextW("--", 4);
        d2->setCursor(226 - tw2, 50);
        d2->print("--");
      }
      // Slim bar.
      d2->fillRoundRect(14, 82, 212, 8, 3, USAGE_COLOR_BG);
      if (pu.fiveHour.available && pu.fiveHour.usedPct > 0) {
        int16_t fw = (int16_t)(204 * (uint32_t)pu.fiveHour.usedPct / 100UL);
        if (fw < 4) fw = 4;
        d2->fillRoundRect(14, 82, fw, 8, 3, barColorFor(pu.fiveHour.usedPct, providerColor, stale));
      }
      // RAM + DISK side by side (half width).
      drawVitalsCard(4,   104, "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(120, 104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      // Banner: battery + uptime.
      drawVitalsBanner(180, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
      return;
    }
    // Partial: data changed → repaint cards + banner.
    if (pu.lastOkMs != lastFiveHourOk_[active_] || stale != lastStale_[active_]) {
      // CPU full width.
      auto* d3 = gfxDev();
      d3->fillRoundRect(4, 42, 232, 56, 5, USAGE_COLOR_CARD);
      d3->setTextColor(USAGE_COLOR_MUTED);
      d3->setTextSize(2);
      d3->setCursor(18, 48);
      d3->print("CPU");
      d3->setTextSize(4);
      if (pu.fiveHour.available) {
        d3->setTextColor(barColorFor(pu.fiveHour.usedPct, providerColor, stale));
        char buf[8]; snprintf(buf, sizeof(buf), "%u%%", pu.fiveHour.usedPct);
        int16_t tw3 = gfxTextW(buf, 4);
        d3->setCursor(226 - tw3, 50);
        d3->print(buf);
      } else {
        d3->setTextColor(USAGE_COLOR_MUTED);
        int16_t tw3 = gfxTextW("--", 4);
        d3->setCursor(226 - tw3, 50);
        d3->print("--");
      }
      d3->fillRoundRect(14, 82, 212, 8, 3, USAGE_COLOR_BG);
      if (pu.fiveHour.available && pu.fiveHour.usedPct > 0) {
        int16_t fw = (int16_t)(204 * (uint32_t)pu.fiveHour.usedPct / 100UL);
        if (fw < 4) fw = 4;
        d3->fillRoundRect(14, 82, fw, 8, 3, barColorFor(pu.fiveHour.usedPct, providerColor, stale));
      }
      drawVitalsCard(4,   104, "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(120, 104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      drawVitalsBanner(180, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
    }
    return;
  }

  // ---- WEATHER: Option A — Time Hero + Icon Card ----
  // Three dirty regions: title pill (stale change), clock (per-minute),
  // weather card (per-push). Never fullScreen per tick.
  if (active_ == PROVIDER_WEATHER) {
    struct tm lt;
    bool synced = clockNow(lt);
    int hh = lt.tm_hour;
    int mm = lt.tm_min;

    if (needsFullRedraw_ || weatherFirstDraw_) {
      needsFullRedraw_   = false;
      weatherFirstDraw_  = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      // Title bar.
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      d->print(s.weather.city.length() ? s.weather.city.c_str() : "BKK");
      lastClockMin_ = 0xFF;
      lastFiveHourOk_[active_] = 0;
      lastStale_[active_] = !stale;  // force pill update
    }

    // Title pill: update when stale status changes.
    if (stale != lastStale_[active_]) {
      auto* d = gfxDev();
      d->fillRect(160, 6, 78, 24, USAGE_COLOR_CARD);
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      lastStale_[active_] = stale;
    }

    // Region 1: Time + date (repaint only when minute changes).
    if (synced && (uint8_t)mm != lastClockMin_) {
      auto* d = gfxDev();
      // Clear time+date area.
      d->fillRect(0, 42, 240, 100, USAGE_COLOR_BG);
      // Time HH:MM — size5, centered, teal.
      d->setTextColor(providerColor);
      d->setTextSize(5);
      char tb[8];
      snprintf(tb, sizeof(tb), "%02d:%02d", hh, mm);
      int16_t tw = gfxTextW(tb, 5);
      d->setCursor((240 - tw) / 2, 44);
      d->print(tb);
      // Date: day of week + full date below (both size3).
      int mon = lt.tm_mon + 1;
      int yr  = lt.tm_year + 1900;
      int dd  = lt.tm_mday;
      int dow = lt.tm_wday;
      static const char* DOWS[] = {"Sunday","Monday","Tuesday","Wednesday",
                                   "Thursday","Friday","Saturday"};
      static const char* MONS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
      d->setTextColor(USAGE_COLOR_TEXT);
      d->setTextSize(3);
      if (dow >= 0 && dow <= 6) {
        int16_t dw = gfxTextW(DOWS[dow], 3);
        d->setCursor((240 - dw) / 2, 94);
        d->print(DOWS[dow]);
      }
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(3);
      char db[24];
      snprintf(db, sizeof(db), "%d %s %d", dd,
               (mon>=1&&mon<=12)?MONS[mon-1]:"---", yr);
      int16_t dw2 = gfxTextW(db, 3);
      d->setCursor((240 - dw2) / 2, 120);
      d->print(db);
      lastClockMin_ = (uint8_t)mm;
    } else if (!synced && lastClockMin_ != 0xFE) {
      auto* d = gfxDev();
      d->fillRect(0, 42, 240, 100, USAGE_COLOR_BG);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      const char* w = "waiting NTP...";
      int16_t tw = gfxTextW(w, 2);
      d->setCursor((240 - tw) / 2, 72);
      d->print(w);
      lastClockMin_ = 0xFE;
    }

    // Region 2: Weather card with icon (repaint when new push lands).
    if (pu.lastOkMs != lastFiveHourOk_[active_]) {
      auto* d = gfxDev();
      // Card: temp + condition + icon, no Hi/Lo.
      d->fillRoundRect(4, 148, 232, 70, 6, USAGE_COLOR_CARD);
      // Icon (left, centered vertically in card).
      if (pu.weatherCode != 0xFF)
        drawWeatherIcon(36, 183, pu.weatherCode, providerColor);
      // Temp, right of icon.
      d->setTextColor(providerColor);
      d->setTextSize(4);
      char tb[8];
      snprintf(tb, sizeof(tb), "%u", pu.fiveHour.usedPct);
      d->setCursor(60, 154);
      d->print(tb);
      d->setTextSize(2);
      d->print("C");
      // Condition label below temp.
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      const char* cond = (pu.weatherCode != 0xFF) ? wmoLabel(pu.weatherCode) : "---";
      d->setCursor(60, 184);
      d->print(cond);
      lastFiveHourOk_[active_] = pu.lastOkMs;
    }

    return;
  }

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

  // (v3.2.7: Age row + AUTO/MANUAL marker removed from y=204..239 — was
  // clutter. LIVE/STALE in the title bar is enough; full age is in WebUI.)
}
