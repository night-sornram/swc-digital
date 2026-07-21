#!/usr/bin/env python3
"""Mac system stats adapter for SWC Digital (3.2+).

Reads CPU + RAM + Storage usage percentages via psutil and returns them in the
same shape as the AI-usage adapters so wifi_usage_service.py can push them
to the device without special-casing:

    {
      "five_hour": {"used_pct": <cpu>},   # CPU shown in the 5H card slot
      "weekly":    {"used_pct": <ram>},   # RAM shown in the Weekly card slot
      "extra_pct": <storage>,              # Storage shown in the 3rd SYSTEM card
    }

psutil is the only third-party dep. CPU% is averaged over a brief
interval (0.5 s) so it is not always 0 or 100.
"""
from __future__ import annotations


class SystemStatsError(Exception):
    """Raised on any failure to read system stats."""


def fetch() -> dict:
    """Return the normalised shape. Raise SystemStatsError on failure."""
    try:
        import psutil
    except ImportError as exc:
        raise SystemStatsError("psutil not installed") from exc

    try:
        cpu = int(round(psutil.cpu_percent(interval=0.5)))
        ram = int(round(psutil.virtual_memory().percent))
        # Root filesystem usage as the SSD metric.
        ssd = int(round(psutil.disk_usage("/").percent))
    except Exception as exc:  # noqa: BLE001 — surface any failure to caller
        raise SystemStatsError(f"psutil read failed: {exc}") from exc

    cpu = max(0, min(100, cpu))
    ram = max(0, min(100, ram))
    ssd = max(0, min(100, ssd))
    return {
        "five_hour": {"used_pct": cpu},
        "weekly":    {"used_pct": ram},
        "extra_pct": ssd,
    }


if __name__ == "__main__":
    import json
    import sys
    try:
        print(json.dumps(fetch()))
    except SystemStatsError as exc:
        print(f"system_stats_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
