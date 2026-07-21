#!/usr/bin/env python3
"""Weather adapter for SWC Digital (3.3+).

Fetches current weather + AQI from open-meteo (free, no API key) and returns
the same dict shape as the other adapters so wifi_usage_service.py can push
it without special-casing:

    {
      "five_hour": {"used_pct": <temp_c>},   # temperature °C (rounded)
      "weekly":    {"used_pct": <eaqi>},     # european AQI index
      "weather_code": <wmo>,                 # 0..99
      "temp_min": <int>,                     # daily low °C
      "temp_max": <int>,                     # daily high °C
      "aqi_pm25": <int>,                     # PM2.5 µg/m³
    }

Two endpoints (confirmed working for Bangkok):
  - api.open-meteo.com/v1/forecast
  - air-quality-api.open-meteo.com/v1/air-quality
"""
from __future__ import annotations

import json
import urllib.request


class WeatherError(Exception):
    """Raised on any failure to fetch weather."""


def _get_json(url: str, timeout: int = 10) -> dict:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception as exc:  # noqa: BLE001
        raise WeatherError(f"fetch failed for {url}: {exc}") from exc


def fetch(lat: float, lon: float) -> dict:
    """Fetch current weather + AQI. Raise WeatherError on failure."""
    weather_url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}"
        f"&current=temperature_2m,weather_code"
        f"&daily=temperature_2m_max,temperature_2m_min"
        f"&timezone=auto"
    )
    aqi_url = (
        f"https://air-quality-api.open-meteo.com/v1/air-quality?"
        f"latitude={lat}&longitude={lon}"
        f"&current=pm2_5,european_aqi"
        f"&timezone=auto"
    )
    w = _get_json(weather_url)
    a = _get_json(aqi_url)

    cur = w.get("current") or {}
    daily = w.get("daily") or {}
    acur = a.get("current") or {}
    if "temperature_2m" not in cur or "european_aqi" not in acur:
        raise WeatherError("incomplete API response")

    temp_c = int(round(cur["temperature_2m"]))
    wmo = int(cur.get("weather_code", 0))
    tmax = int(round((daily.get("temperature_2m_max") or [0])[0]))
    tmin = int(round((daily.get("temperature_2m_min") or [0])[0]))
    eaqi = int(round(acur["european_aqi"]))
    pm25 = int(round(acur.get("pm2_5", 0)))

    return {
        "five_hour": {"used_pct": temp_c},
        "weekly":    {"used_pct": eaqi},
        "weather_code": wmo,
        "temp_min": tmin,
        "temp_max": tmax,
        "aqi_pm25": pm25,
    }


if __name__ == "__main__":
    import sys
    try:
        print(json.dumps(fetch(13.7563, 100.5018)))
    except WeatherError as exc:
        print(f"weather_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
