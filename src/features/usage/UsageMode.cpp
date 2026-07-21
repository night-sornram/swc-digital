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

// WMO weather code → 3-char label (built-in font lacks weather glyphs).
// See open-meteo docs: 0=clear, 1-3=cloudy, 45-48=fog, 51-67=rain,
// 71-77=snow, 80-82=showers, 95-99=thunderstorm.
static const char* wmoLabel(uint8_t code) {
  if (code == 0)                       return "CLR";
  if (code >= 1 && code <= 3)          return "CLD";
  if (code >= 45 && code <= 48)        return "FOG";
  if (code >= 51 && code <= 67)        return "RAIN";
  if (code >= 71 && code <= 77)        return "SNOW";
  if (code >= 80 && code <= 82)        return "SHR";
  if (code >= 95)                      return "STM";
  return "--";
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
  const int16_t by = y + 42, bh = 8, bx = 18, bw = 204;
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
  d->fillRoundRect(8, y, 224, 50, 5, USAGE_COLOR_CARD);
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
    d->setCursor(222 - tw, y + 6);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "N/A";
    int16_t tw = gfxTextW(na, 3);
    d->setCursor(222 - tw, y + 6);
    d->print(na);
  }
  // Slim bar.
  const int16_t by = y + 34, bh = 8, bx = 18, bw = 204;
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
  d->fillRoundRect(x, y, 108, 56, 5, USAGE_COLOR_CARD);
  d->setTextColor(USAGE_COLOR_MUTED);
  d->setTextSize(2);
  d->setCursor(x + 8, y + 6);
  d->print(label);
  d->setTextSize(3);
  if (avail) {
    d->setTextColor(barColorFor(pct, providerColor, stale));
    char buf[10];
    if (isTemp) {
      // Degree symbol: the built-in font's degree (0xF8 in some codepages) may
      // not render. Use a lowercase 'c' suffix as a safe fallback: "54c".
      // (Verified visually in integration test — adjust if wrong.)
      snprintf(buf, sizeof(buf), "%dc", (int)(int8_t)pct);
    } else {
      snprintf(buf, sizeof(buf), "%u%%", pct);
    }
    int16_t tw = gfxTextW(buf, 3);
    d->setCursor(x + 100 - tw, y + 6);
    d->print(buf);
  } else {
    d->setTextColor(USAGE_COLOR_MUTED);
    const char* na = "--";
    int16_t tw = gfxTextW(na, 3);
    d->setCursor(x + 100 - tw, y + 6);
    d->print(na);
  }
  // Slim bar (only for % metrics, not temp).
  if (!isTemp) {
    const int16_t by = y + 44, bh = 6, bx = x + 8, bw = 92;
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
  d->fillRoundRect(8, y, 224, 30, 5, USAGE_COLOR_CARD);
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

void UsageMode::toggleAutoProvider() {
  // Rotate CODEX → ZAI → VITALS → WEATHER → CODEX.
  switch (active_) {
    case PROVIDER_CODEX:   active_ = PROVIDER_ZAI;     break;
    case PROVIDER_ZAI:     active_ = PROVIDER_VITALS;  break;
    case PROVIDER_VITALS:  active_ = PROVIDER_WEATHER; break;
    case PROVIDER_WEATHER:
    default:               active_ = PROVIDER_CODEX;   break;
  }
  needsFullRedraw_ = true;
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
      // Row 1: CPU (left) / RAM (right).
      drawVitalsCard(8,   42,  "CPU",  pu.fiveHour.usedPct, pu.fiveHour.available, false, providerColor, stale);
      drawVitalsCard(124, 42,  "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      // Row 2: SSD (left) / TEMP (right).
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(8,   104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      bool tempAvail = (pu.tempC != (int8_t)0x80);
      drawVitalsCard(124, 104, "TEMP", (uint8_t)pu.tempC,   tempAvail,             true,  providerColor, stale);
      // Banner: battery + uptime.
      drawVitalsBanner(166, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
      return;
    }
    // Partial: data changed → repaint the 4 cards + banner.
    if (pu.lastOkMs != lastFiveHourOk_[active_] || stale != lastStale_[active_]) {
      drawVitalsCard(8,   42,  "CPU",  pu.fiveHour.usedPct, pu.fiveHour.available, false, providerColor, stale);
      drawVitalsCard(124, 42,  "RAM",  pu.weekly.usedPct,   pu.weekly.available,   false, providerColor, stale);
      bool ssdAvail = (pu.extraPct != 0xFF);
      drawVitalsCard(8,   104, "DISK", pu.extraPct,         ssdAvail,              false, providerColor, stale);
      bool tempAvail = (pu.tempC != (int8_t)0x80);
      drawVitalsCard(124, 104, "TEMP", (uint8_t)pu.tempC,   tempAvail,             true,  providerColor, stale);
      drawVitalsBanner(166, pu.batteryPct, pu.uptimeMin, stale);
      lastFiveHourOk_[active_] = pu.lastOkMs;
      lastStale_[active_]      = stale;
    }
    return;
  }

  // ---- WEATHER: Clock hero + Mini calendar ----
  // Three independent dirty regions: clock (per-minute), weather+AQI (per-push),
  // week-strip calendar (per-day). Never fillScreen per tick. Full redraw only
  // on mode enter.
  if (active_ == PROVIDER_WEATHER) {
    // Local time from device NTP (not pushed).
    struct tm lt;
    bool synced = clockNow(lt);
    int hh = lt.tm_hour;
    int mm = lt.tm_min;
    int dd = lt.tm_mday;

    if (needsFullRedraw_ || weatherFirstDraw_) {
      needsFullRedraw_   = false;
      weatherFirstDraw_  = false;
      auto* d = gfxDev();
      d->fillScreen(USAGE_COLOR_BG);
      // Title bar: city label from settings (defaults to "BKK" if empty).
      d->fillRect(0, 0, 240, 35, USAGE_COLOR_CARD);
      d->setTextColor(providerColor);
      d->setTextSize(3);
      d->setCursor(10, 8);
      d->print(s.weather.city.length() ? s.weather.city.c_str() : "BKK");
      d->setTextSize(2);
      const char* pill = stale ? "STALE" : "LIVE";
      d->setTextColor(stale ? USAGE_COLOR_STALE : USAGE_COLOR_TEXT);
      int16_t tw = gfxTextW(pill, 2);
      d->setCursor(232 - tw, 10);
      d->print(pill);
      // Force all three regions to paint by seeding last values.
      lastClockMin_ = 0xFF;
      lastClockDay_ = 0xFF;
      lastFiveHourOk_[active_] = 0;
    }

    // Region 1: Clock (repaint only when minute changes).
    if (synced && (uint8_t)mm != lastClockMin_) {
      auto* d = gfxDev();
      // Clear clock area (y=42..105).
      d->fillRect(0, 42, 240, 64, USAGE_COLOR_BG);
      // Time HH:MM centered, size 4.
      d->setTextColor(providerColor);
      d->setTextSize(4);
      char tb[8];
      snprintf(tb, sizeof(tb), "%02d:%02d", hh, mm);
      int16_t tw = gfxTextW(tb, 4);
      d->setCursor((240 - tw) / 2, 50);
      d->print(tb);
      // Date below, size 2, muted.
      int mon = lt.tm_mon + 1;    // 1..12
      int yr  = lt.tm_year + 1900;
      int dow = lt.tm_wday;       // 0=Sun..6=Sat
      static const char* DOWS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      static const char* MONS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      char db[24];
      snprintf(db, sizeof(db), "%s %d %s %d",
               (dow>=0&&dow<=6)?DOWS[dow]:"---", dd,
               (mon>=1&&mon<=12)?MONS[mon-1]:"---", yr);
      int16_t dw = gfxTextW(db, 2);
      d->setCursor((240 - dw) / 2, 90);
      d->print(db);
      lastClockMin_ = (uint8_t)mm;
    } else if (!synced && lastClockMin_ != 0xFE) {
      // Not synced: show waiting text once.
      auto* d = gfxDev();
      d->fillRect(0, 42, 240, 64, USAGE_COLOR_BG);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      const char* w = "waiting NTP...";
      int16_t tw = gfxTextW(w, 2);
      d->setCursor((240 - tw) / 2, 65);
      d->print(w);
      lastClockMin_ = 0xFE;
    }

    // Region 2: Weather + AQI (repaint when new push lands).
    if (pu.lastOkMs != lastFiveHourOk_[active_]) {
      auto* d = gfxDev();
      // Card y=112..160.
      d->fillRoundRect(8, 112, 224, 48, 5, USAGE_COLOR_CARD);
      // Left half: temp + condition.
      d->setTextColor(providerColor);
      d->setTextSize(3);
      char tb[8];
      snprintf(tb, sizeof(tb), "%uc", pu.fiveHour.usedPct);  // temp °C (use 'c' suffix)
      d->setCursor(18, 116);
      d->print(tb);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setTextSize(2);
      d->setCursor(18, 142);
      if (pu.weatherCode != 0xFF) d->print(wmoLabel(pu.weatherCode));
      else                         d->print("--");
      // Divider.
      d->drawFastVLine(120, 116, 40, USAGE_COLOR_BG);
      // Right half: AQI index + PM2.5.
      uint8_t eaqi = pu.weekly.usedPct;
      d->setTextColor(aqiColor(eaqi));
      d->setTextSize(3);
      char ab[8];
      snprintf(ab, sizeof(ab), "%u", eaqi);
      int16_t aw = gfxTextW(ab, 3);
      d->setCursor(232 - aw, 116);
      d->print(ab);
      d->setTextSize(2);
      d->setTextColor(USAGE_COLOR_MUTED);
      d->setCursor(130, 116);
      d->print("AQI");
      if (pu.aqiPm25 != 0xFF) {
        char pb[16];
        snprintf(pb, sizeof(pb), "PM %u", pu.aqiPm25);
        d->setCursor(130, 142);
        d->print(pb);
      }
      lastFiveHourOk_[active_] = pu.lastOkMs;
    }

    // Region 3: Mini calendar week strip (repaint when day changes).
    if (synced && (uint8_t)dd != lastClockDay_) {
      auto* d = gfxDev();
      // Card y=166..226.
      d->fillRoundRect(8, 166, 224, 60, 5, USAGE_COLOR_CARD);
      int dow = lt.tm_wday;   // 0=Sun..6=Sat
      // We want Mon-Sun strip with today highlighted.
      // Offset from Monday: (dow+6)%7
      int off = (dow + 6) % 7;
      static const char* DOW3 = "MTWTFSS";
      d->setTextSize(2);
      for (int i = 0; i < 7; i++) {
        int x = 14 + i * 31;
        // Day number (size 2 = readable).
        int slotDay = dd - off + i;
        bool isToday = (i == off);
        if (isToday) {
          d->fillRoundRect(x - 2, 178, 28, 24, 3, providerColor);
          d->setTextColor(USAGE_COLOR_BG);
        } else {
          d->setTextColor(USAGE_COLOR_TEXT);
        }
        char db[4];
        snprintf(db, sizeof(db), "%d", slotDay);
        int16_t dw = gfxTextW(db, 2);
        d->setCursor(x + (28 - dw) / 2, 180);
        d->print(db);
        // DOW label (small, under the number).
        d->setTextSize(1);
        d->setTextColor(USAGE_COLOR_MUTED);
        char dl[2] = {DOW3[i], 0};
        d->setCursor(x + 10, 204);
        d->print(dl);
        d->setTextSize(2);
      }
      lastClockDay_ = (uint8_t)dd;
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
