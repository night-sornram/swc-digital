#!/usr/bin/env python3
"""Codex usage adapter for the Wi-Fi push service (SWC Digital 3.0.0).

Reads the OAuth access token from ~/.codex/auth.json in memory, queries the
Codex usage endpoint, and returns normalised windows:

    {
      "five_hour": {"used_pct": int, "reset_min": int} | None,
      "weekly":    {"used_pct": int, "reset_min": int} | None,
    }

The endpoint is a consumer endpoint (chatgpt.com/backend-api/wham/usage) and is
NOT a stable public API; it is kept entirely behind this adapter so callers do
not depend on its shape. The script never logs the token, the Authorization
header, account ids, or the full response body.
"""
from __future__ import annotations

import json
import math
import os
import urllib.error
import urllib.request
from pathlib import Path
from typing import Optional

AUTH_PATH = Path(os.environ.get("CODEX_AUTH_PATH", Path.home() / ".codex" / "auth.json"))
USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"

# limit_window_seconds -> our window name. Other values are ignored.
WINDOW_BY_SECONDS = {18000: "five_hour", 604800: "weekly"}


class CodexAdapterError(Exception):
    """Raised on auth-read failure, transport error, or malformed response."""


def _read_token() -> str:
    try:
        with AUTH_PATH.expanduser().open() as handle:
            token = json.load(handle)["tokens"]["access_token"]
    except (FileNotFoundError, KeyError, json.JSONDecodeError, TypeError) as exc:
        raise CodexAdapterError("codex auth unavailable") from exc
    if not isinstance(token, str) or not token:
        raise CodexAdapterError("codex access token missing")
    return token


def _to_window(payload: dict) -> Optional[dict]:
    """Map one rate-limit window payload to our normalised shape."""
    try:
        pct = payload["used_percent"]
    except (KeyError, TypeError):
        return None
    if (
        not isinstance(pct, (int, float))
        or isinstance(pct, bool)
        or not math.isfinite(pct)
        or not 0 <= round(pct) <= 100
    ):
        return None
    out: dict = {"used_pct": int(round(pct))}
    reset_s = payload.get("reset_after_seconds") if isinstance(payload, dict) else None
    if (
        isinstance(reset_s, (int, float))
        and not isinstance(reset_s, bool)
        and math.isfinite(reset_s)
        and reset_s >= 0
    ):
        # Round UP per spec.
        out["reset_min"] = int(min(math.ceil(reset_s / 60), 65535))
    return out


def fetch() -> dict:
    """Return normalised windows. Raise CodexAdapterError on any failure."""
    req = urllib.request.Request(
        USAGE_URL,
        headers={"Authorization": f"Bearer {_read_token()}", "User-Agent": "codex-cli"},
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as response:
            data = json.load(response)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
        # Do not include the body or headers in the message.
        status = getattr(getattr(exc, "response", None), "status", None) or getattr(exc, "code", None)
        raise CodexAdapterError(f"codex request failed (status={status})") from exc

    rate_limit = data.get("rate_limit") if isinstance(data, dict) else None
    if not isinstance(rate_limit, dict):
        raise CodexAdapterError("codex response missing rate_limit")

    out: dict = {"five_hour": None, "weekly": None}
    # Check both windows; classify by limit_window_seconds.
    for key in ("primary_window", "secondary_window"):
        win = rate_limit.get(key)
        if not isinstance(win, dict):
            continue
        secs = win.get("limit_window_seconds")
        name = WINDOW_BY_SECONDS.get(secs) if isinstance(secs, int) else None
        if not name or out[name] is not None:
            continue
        normalised = _to_window(win)
        if normalised is not None:
            out[name] = normalised
    return out


if __name__ == "__main__":
    # Manual smoke test: print the normalised shape only (no token, no raw body).
    import sys
    try:
        print(json.dumps(fetch()))
    except CodexAdapterError as exc:
        print(f"codex_wifi_adapter: {exc}", file=sys.stderr)
        sys.exit(2)
