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
        # CPU: sample over 1.5s for a stable reading (0.5s often returns 0%
        # on an idle Mac). The brief block is fine — the service polls
        # every 60s.
        cpu = int(round(psutil.cpu_percent(interval=1.5)))
        # RAM: psutil.virtual_memory().percent ≈ Activity Monitor (uses
        # active+wired+compressed, not inactive). Matches within ~2%.
        ram = int(round(psutil.virtual_memory().percent))
        # macOS splits the SSD into a read-only system volume (/) and a data
        # volume (/System/Volumes/Data) since Catalina. psutil.disk_usage('/')
        # only sees the system volume (tiny, ~7%). Use the data volume when
        # it exists so the percentage matches what Finder/About-This-Mac show.
        # Note: os.path.ismount() returns False for firmlinks, so we check
        # existence instead. Linux/other: just use '/'.
        import os
        disk_path = "/System/Volumes/Data"
        if not os.path.exists(disk_path):
            disk_path = "/"
        du = psutil.disk_usage(disk_path)
        # psutil.percent uses used/(used+free) which over-reports vs Finder
        # (Finder = used/total, where used excludes APFS snapshots/clones).
        # Use used/total for consistency with what the user sees in Finder
        # and About This Mac > Storage.
        ssd = int(round(du.used / du.total * 100))
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
