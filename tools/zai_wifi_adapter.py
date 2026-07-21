#!/usr/bin/env python3
"""z.ai usage adapter for the Wi-Fi push service (SWC Digital 3.0.0).

Reads ANTHROPIC_BASE_URL and ANTHROPIC_AUTH_TOKEN from ~/.claude/settings.json
in memory, allows only host api.z.ai, queries the quota endpoint, and returns
normalised windows:

    {
      "five_hour": {"used_pct": int, "reset_min": int} | None,
      "weekly":    {"used_pct": int, "reset_min": int} | None,
    }

Quota schema (confirmed against ZCode):
  data.limits[] entries have type "TOKENS_LIMIT".
  5H window:     type=TOKENS_LIMIT, unit=3, number=5
  Weekly window: type=TOKENS_LIMIT, unit=6

Never logs the token, the Authorization header, account ids, or the full body.
"""
from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

SETTINGS_PATH = Path(os.environ.get("CLAUDE_SETTINGS_PATH", Path.home() / ".claude" / "settings.json"))
ALLOWED_HOST = "api.z.ai"
QUOTA_PATH = "/api/monitor/usage/quota/limit"


class ZaiAdapterError(Exception):
    """Raised on settings-read failure, transport error, or disallowed host."""


def _read_settings() -> tuple[str, str]:
    try:
        with SETTINGS_PATH.expanduser().open() as handle:
            cfg = json.load(handle)
    except (FileNotFoundError, json.JSONDecodeError, TypeError) as exc:
        raise ZaiAdapterError("z.ai settings unavailable") from exc
    env = cfg.get("env") if isinstance(cfg, dict) else None
    if not isinstance(env, dict):
        env = cfg if isinstance(cfg, dict) else {}
    base = env.get("ANTHROPIC_BASE_URL")
    token = env.get("ANTHROPIC_AUTH_TOKEN")
    if not isinstance(base, str) or not isinstance(token, str) or not token:
        raise ZaiAdapterError("z.ai base url or token missing")
    # Enforce host allowlist. base may include scheme + path; verify host only.
    from urllib.parse import urlparse
    host = urlparse(base).hostname or ""
    if host != ALLOWED_HOST:
        raise ZaiAdapterError("z.ai host not allowed")
    return base.rstrip("/"), token


def _classify_limit(entry: dict) -> Optional[str]:
    """Return 'five_hour' / 'weekly' / None for one data.limits entry."""
    if entry.get("type") != "TOKENS_LIMIT":
        return None
    unit = entry.get("unit")
    number = entry.get("number")
    if unit == 3 and number == 5:
        return "five_hour"
    if unit == 6:
        return "weekly"
    return None


def _to_window(entry: dict) -> Optional[dict]:
    pct = entry.get("percentage")
    if (
        not isinstance(pct, (int, float))
        or isinstance(pct, bool)
        or not 0 <= round(pct) <= 100
    ):
        return None
    out: dict = {"used_pct": int(round(pct))}
    reset = entry.get("nextResetTime")
    if isinstance(reset, str) and reset:
        try:
            # ISO-8601 (z.ai uses e.g. "2026-07-22T03:00:00Z"). Be tolerant of offsets.
            dt = datetime.fromisoformat(reset.replace("Z", "+00:00"))
            now = datetime.now(timezone.utc)
            mins = int((dt - now).total_seconds() // 60)
            out["reset_min"] = max(0, min(65535, mins))
        except (ValueError, TypeError):
            pass
    return out


def fetch() -> dict:
    """Return normalised windows. Raise ZaiAdapterError on any failure."""
    base, token = _read_settings()
    url = f"https://{ALLOWED_HOST}{QUOTA_PATH}"   # always https + allowed host
    req = urllib.request.Request(
        url,
        headers={"Authorization": token},
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as response:
            data = json.load(response)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
        status = getattr(getattr(exc, "response", None), "status", None) or getattr(exc, "code", None)
        raise ZaiAdapterError(f"z.ai request failed (status={status})") from exc

    out: dict = {"five_hour": None, "weekly": None}
    limits = (data.get("data") or {}).get("limits") if isinstance(data, dict) else None
    if not isinstance(limits, list):
        return out   # no limits -> both windows null
    for entry in limits:
        if not isinstance(entry, dict):
            continue
        name = _classify_limit(entry)
        if name and out[name] is None:
            window = _to_window(entry)
            if window is not None:
                out[name] = window
    return out


if __name__ == "__main__":
    import sys
    try:
        print(json.dumps(fetch()))
    except ZaiAdapterError as exc:
        print(f"zai_wifi_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
