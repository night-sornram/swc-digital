#!/usr/bin/env python3
"""Mac vitals adapter for SWC Digital (3.3+).

Returns the same dict shape as system_stats_adapter so wifi_usage_service.py
can push it without special-casing:

    {
      "five_hour": {"used_pct": <cpu>},     # CPU %
      "weekly":    {"used_pct": <ram>},     # RAM %
      "extra_pct": <ssd>,                   # SSD %
      "temp_c":    <int or None>,           # Mac temp; None on Apple Silicon
      "battery_pct": <0..100 or None>,
      "uptime_min": <int>,
    }

psutil is the only third-party dep. CPU% is a 3-sample average (smooths
spikes). Temperature is unavailable on Apple Silicon via psutil
(sensors_temperatures() is missing) — returns None so the device shows '--'.
"""
from __future__ import annotations

import os
import time


class VitalsError(Exception):
    """Raised on any failure to read vitals."""


def fetch() -> dict:
    """Return the normalised shape. Raise VitalsError on failure."""
    try:
        import psutil
    except ImportError as exc:
        raise VitalsError("psutil not installed") from exc

    try:
        # CPU: 3 samples × 0.5s averaged (same logic as system_stats_adapter).
        samples = [psutil.cpu_percent(interval=0.5) for _ in range(3)]
        cpu = int(round(sum(samples) / len(samples)))
        ram = int(round(psutil.virtual_memory().percent))
        # SSD: use the data volume on macOS so the % matches Finder.
        disk_path = "/System/Volumes/Data"
        if not os.path.exists(disk_path):
            disk_path = "/"
        du = psutil.disk_usage(disk_path)
        ssd = int(round(du.used / du.total * 100))
        # Battery (laptops; desktops return None).
        bat = psutil.sensors_battery()
        battery_pct = int(round(bat.percent)) if bat is not None else None
        # Uptime in minutes.
        uptime_min = int((time.time() - psutil.boot_time()) / 60)
    except Exception as exc:  # noqa: BLE001 — surface any failure
        raise VitalsError(f"psutil read failed: {exc}") from exc

    # Temperature: psutil.sensors_temperatures() does not exist on Apple
    # Silicon (darwin). Return None — the device renders '--'.
    temp_c = None

    cpu = max(0, min(100, cpu))
    ram = max(0, min(100, ram))
    ssd = max(0, min(100, ssd))
    if battery_pct is not None:
        battery_pct = max(0, min(100, battery_pct))
    return {
        "five_hour": {"used_pct": cpu},
        "weekly":    {"used_pct": ram},
        "extra_pct": ssd,
        "temp_c": temp_c,
        "battery_pct": battery_pct,
        "uptime_min": uptime_min,
    }


if __name__ == "__main__":
    import json
    import sys
    try:
        print(json.dumps(fetch()))
    except VitalsError as exc:
        print(f"vitals_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
