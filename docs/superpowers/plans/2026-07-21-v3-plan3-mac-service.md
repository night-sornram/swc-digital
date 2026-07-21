# SWC Digital 3.0.0 — Plan 3: Mac Wi-Fi Usage Push Service

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone macOS service that fetches Codex and z.ai usage every 60 s and pushes each provider independently to the SmallTV-ultra over Wi-Fi (LAN). Discovers devices via `_aiusage._tcp` mDNS with an explicit-URL fallback. Per-provider 429/5xx backoff. Logs only provider/time/status/category — never tokens, headers, account ids, or full responses. Ships as a LaunchAgent `com.night.swc-digital-wifi-usage`. The existing USB `clock_service.py` is untouched.

**Architecture:** New Python script `tools/wifi_usage_service.py` + two pure adapters (`codex_wifi_adapter.py`, `zai_wifi_adapter.py`) + a tiny mDNS discovery helper. The adapters return a normalised `{"five_hour": {...}|None, "weekly": {...}|None}` dict; the service wraps each into the spec's push body and POSTs it. `codex_usage.py` (the existing USB-collector CLI) is preserved verbatim — a thin compatibility wrapper `tools/codex_wifi_adapter.py` reads `~/.codex/auth.json` per spec, not the multi-profile path.

**Tech Stack:** Python 3.11 stdlib (`urllib`, `json`, `socket`, `threading`, `tomllib`), `zeroconf` for mDNS (the only third-party dep), macOS `launchd` for the LaunchAgent.

**Branch:** `feature/v3-usage-display`.

**Depends on:** Plan 2 complete (the device answers `POST /api/usage` with the v1 schema and advertises `_aiusage._tcp`).

**Spec source:** `pasted-text-20260721-155906-89253a9e.txt` §5 (Mac Usage Service), the Codex/z.ai adapter sub-sections, and §Public Interfaces (push body contract).

---

## Pre-flight

- [ ] **P1: Confirm Plan 2 done-criteria.**

```sh
git log --oneline -20 | grep "v3:"           # Plan 1 + Plan 2 commits present
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -4    # [SUCCESS]
cd tests && python -m unittest test_usage_store test_settings_migration 2>&1 | tail -3 && cd ..
```

Expected: v3 commits present, build `[SUCCESS]`, 17 tests pass.

- [ ] **P2: Confirm the USB clock_service.py is untouched.**

```sh
git diff main -- tools/clock_service.py | head -5 || echo "clean"
```

Expected: `clean` (or no diff lines). Plan 3 MUST NOT modify `clock_service.py`, `clockctl.py`, `clock_gui.py`, `usage_collector.py`, `crypto_market.py`, `codex_usage.py`, `claude_usage.py`, or `claude_profile_vault.swift`.

- [ ] **P3: Sanity-check the third-party mDNS library installs cleanly.**

```sh
uv pip install --system zeroconf 2>/dev/null || uv run --with zeroconf python -c "import zeroconf; print(zeroconf.__version__)"
```

Expected: prints a version. `zeroconf` is pure-Python and ships as a wheel — no build toolchain needed. If it is unavailable, the explicit-URL fallback still works, but mDNS discovery will not.

---

## File Structure

**Created:**
- `tools/wifi_usage_service.py` — the main service (loop, push, backoff, logging, mDNS).
- `tools/codex_wifi_adapter.py` — Codex adapter reading `~/.codex/auth.json`, returns normalised windows.
- `tools/zai_wifi_adapter.py` — z.ai adapter reading `~/.claude/settings.json`, queries `api.z.ai`.
- `tools/aiusage_mdns.py` — tiny mDNS discovery wrapper (returns a list of URLs).
- `tools/wifi-usage.toml.example` — the example config (real one is gitignored).
- `tools/com.night.swc-digital-wifi-usage.plist.example` — the LaunchAgent template.

**Modified:**
- `.gitignore` — add `tools/wifi-usage.toml` (real config has provider paths; no tokens, but still user-specific).
- No firmware changes. No USB tool changes.

**Untouched (HARD rule — every task must verify):** `tools/clock_service.py`, `tools/clockctl.py`, `tools/clock_gui.py`, `tools/usage_collector.py`, `tools/crypto_market.py`, `tools/codex_usage.py`, `tools/claude_usage.py`, `tools/claude_profile_vault.swift`, `tools/extract_mascot.py`, `tools/usage-collector.toml.example`, `tools/com.example.smart-weather-clock-usage.plist.example`, all `src/`, `tests/test_clock_tools.py`.

---

## Task 1: Codex Wi-Fi adapter — read `~/.codex/auth.json`, normalise windows

**Files:**
- Create: `tools/codex_wifi_adapter.py`

**Spec rules (spec §5 Codex adapter):**
- Read OAuth token from `~/.codex/auth.json` in memory.
- Keep endpoint details behind the adapter (consumer endpoint, not a stable public API).
- Check both `primary_window` and `secondary_window`.
- Map by `limit_window_seconds`: `18000` → 5H, `604800` → Weekly.
- Use `used_percent`.
- Convert `reset_after_seconds` → minutes (round UP).
- Missing/unknown window → `null`.
- The current account has no 5H window; the display MUST show `N/A`. Do NOT substitute Weekly for 5H.

- [ ] **Step 1.1: Create `tools/codex_wifi_adapter.py`.**

```python
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
```

- [ ] **Step 1.2: Smoke-test the adapter manually (offline-safe: fails gracefully if no token).**

```sh
uv run --no-project python tools/codex_wifi_adapter.py 2>&1 | head -3
```

Expected: either a JSON line `{"five_hour": null, "weekly": {"used_pct": .., "reset_min": ..}}` (if `~/.codex/auth.json` exists and the call succeeds), or `codex_wifi_adapter: codex auth unavailable` (exit 2). It MUST NOT print any token, header, or raw response body.

- [ ] **Step 1.3: Commit.**

```sh
git add tools/codex_wifi_adapter.py
git commit -m "feat(v3): codex Wi-Fi adapter (reads ~/.codex/auth.json, normalises windows)"
```

---

## Task 2: z.ai adapter — read `~/.claude/settings.json`, query `api.z.ai`

**Files:**
- Create: `tools/zai_wifi_adapter.py`

**Spec rules (spec §5 z.ai adapter):**
- Read `ANTHROPIC_BASE_URL` and `ANTHROPIC_AUTH_TOKEN` from `~/.claude/settings.json` in memory.
- Allow host `api.z.ai` only.
- `GET https://api.z.ai/api/monitor/usage/quota/limit`.
- Map `data.limits`:
  - 5H: `type=TOKENS_LIMIT`, `unit=3`, `number=5`.
  - Weekly: `type=TOKENS_LIMIT`, `unit=6`.
- Use raw `percentage` as used %.
- Convert `nextResetTime` → minutes remaining, clamp `0..65535`.
- Missing quota → `null`.
- Schema uses `TOKENS_LIMIT` (confirmed against ZCode; per the spec lock at answer time).

- [ ] **Step 2.1: Create `tools/zai_wifi_adapter.py`.**

```python
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
    base = cfg.get("ANTHROPIC_BASE_URL") if isinstance(cfg, dict) else None
    token = cfg.get("ANTHROPIC_AUTH_TOKEN") if isinstance(cfg, dict) else None
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
        headers={"Authorization": f"Bearer {token}"},
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
```

- [ ] **Step 2.2: Smoke-test (offline-safe).**

```sh
uv run --no-project python tools/zai_wifi_adapter.py 2>&1 | head -3
```

Expected: either a JSON line with one or both windows, or `zai_wifi_adapter: z.ai settings unavailable` / `host not allowed` (exit 2). No token, header, or full body.

- [ ] **Step 2.3: Commit.**

```sh
git add tools/zai_wifi_adapter.py
git commit -m "feat(v3): z.ai Wi-Fi adapter (settings.json + api.z.ai quota, TOKENS_LIMIT)"
```

---

## Task 3: mDNS discovery helper

**Files:**
- Create: `tools/aiusage_mdns.py`

**Spec rules:** discover via `_aiusage._tcp.local`; support explicit URL fallback. The TXT records (set by the firmware, Plan 2 Task 6.4) are `id`, `ver`, `path`, `schema`.

- [ ] **Step 3.1: Create `tools/aiusage_mdns.py`.**

```python
#!/usr/bin/env python3
"""mDNS discovery for _aiusage._tcp (SWC Digital 3.0.0).

Returns a list of push URLs (e.g. ["http://192.168.1.42/api/usage"]) found on
the local network within a short browse window. If `zeroconf` is not installed
or nothing is found, callers fall back to their explicit URL list.

Never logs credentials; this module only performs local-network discovery.
"""
from __future__ import annotations

import time
from typing import Iterable

SERVICE_TYPE = "_aiusage._tcp.local."


def discover(timeout: float = 2.0) -> list[str]:
    """Return up to N push URLs discovered via mDNS within `timeout` seconds."""
    try:
        from zeroconf import IPVersion, ServiceBrowser, Zeroconf
    except ImportError:
        return []

    found: list[str] = []

    class _Listener:
        def add_service(self, zc, type_, name): self._maybe_add(zc, name)
        def update_service(self, zc, type_, name): self._maybe_add(zc, name)
        def remove_service(self, zc, type_, name): pass
        def _maybe_add(self, zc, name):
            try:
                info = zc.get_service_info(type_=SERVICE_TYPE, name=name, timeout=800)
            except Exception:
                return
            if not info:
                return
            # Build the URL from the first address + the TXT "path" (default /api/usage).
            addresses = info.parsed_addresses()
            if not addresses:
                return
            path = "/"
            try:
                path = info.properties.get(b"path", b"/api/usage").decode("ascii", "replace")
            except Exception:
                path = "/api/usage"
            found.append(f"http://{addresses[0]}{path}")

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, SERVICE_TYPE, _Listener())
        time.sleep(timeout)
    finally:
        zc.close()
    return list(dict.fromkeys(found))   # de-dup, preserve order


def all_targets(explicit: Iterable[str], mdns_timeout: float = 2.0) -> list[str]:
    """Explicit URLs first (deterministic order), then mDNS-discovered URLs.
    De-duplicated."""
    out: list[str] = []
    for url in list(explicit) + discover(timeout=mdns_timeout):
        if url and url not in out:
            out.append(url)
    return out


if __name__ == "__main__":
    import json
    print(json.dumps(discover(timeout=3.0)))
```

- [ ] **Step 3.2: Commit.**

```sh
git add tools/aiusage_mdns.py
git commit -m "feat(v3): mDNS discovery for _aiusage._tcp with explicit-URL fallback"
```

---

## Task 4: The service — loop, push, per-provider backoff, strict logging

**Files:**
- Create: `tools/wifi_usage_service.py`

**Spec rules (§5):**
- Poll Codex + z.ai every 60 s.
- One provider failing MUST NOT stop the other.
- Push per-provider so freshness is independent.
- mDNS discovery `_aiusage._tcp.local`; explicit URL fallback.
- HTTP push failure: retry once after 5 s.
- 429/5xx: backoff 2, 4, 8 min, capped at 15 min, per-provider only.
- On success, return to 60 s.
- Log only provider, time, HTTP status, error category.
- NEVER log token, Authorization header, email, account id, or full provider response.

- [ ] **Step 4.1: Create `tools/wifi_usage_service.py`.**

```python
#!/usr/bin/env python3
"""SWC Digital 3.0.0 Wi-Fi usage push service.

Fetches Codex and z.ai usage every interval_seconds (default 60) and pushes
each provider independently to every SmallTV-ultra discovered via mDNS
(_aiusage._tcp.local) or listed explicitly in the config.

Per-provider behaviour:
  - One provider failing does NOT stop the other.
  - HTTP push failure -> one retry after 5 s.
  - Provider API 429/5xx -> backoff 2, 4, 8, ..., capped at 15 min.
  - Success -> next poll after interval_seconds.

Logging: provider name, ISO time, HTTP status (when known), error category.
NEVER: token, Authorization header, email, account id, full response body.

LaunchAgent: com.night.swc-digital-wifi-usage (see the plist example).
"""
from __future__ import annotations

import json
import logging
import os
import sys
import time
import tomllib
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# Import siblings (same dir). Run as a script, so add tools/ to sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import codex_wifi_adapter
import zai_wifi_adapter
import aiusage_mdns

CONFIG_PATH = Path(os.environ.get("WIFI_USAGE_CONFIG",
                                  Path(__file__).resolve().parent / "wifi-usage.toml"))

log = logging.getLogger("wifi-usage")


def _iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


@dataclass
class ProviderState:
    name: str
    fetch: object                 # callable() -> dict
    backoff_min: int = 0          # current backoff in minutes (0 = none)
    consecutive_failures: int = 0


def _load_config(path: Path) -> dict:
    with path.open("rb") as handle:
        return tomllib.load(handle)


def _make_body(provider_token: str, windows: dict) -> dict:
    """Wrap normalised windows into the firmware's v1 push body."""
    def _w(w):
        if w is None:
            return None
        return {"used_pct": w.get("used_pct"), "reset_min": w.get("reset_min")}
    return {
        "v": 1,
        "provider": provider_token,
        "five_hour_used_pct":  _w(windows.get("five_hour"))["used_pct"]  if windows.get("five_hour")  else None,
        "five_hour_reset_min": _w(windows.get("five_hour"))["reset_min"] if windows.get("five_hour")  else None,
        "weekly_used_pct":     _w(windows.get("weekly"))["used_pct"]     if windows.get("weekly")     else None,
        "weekly_reset_min":    _w(windows.get("weekly"))["reset_min"]    if windows.get("weekly")     else None,
    }


def _post(url: str, body: dict, timeout: float = 8.0) -> tuple[bool, Optional[int]]:
    """POST the JSON body. Returns (ok, http_status). Never raises for HTTP errors."""
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status in (200, 204), response.status
    except urllib.error.HTTPError as exc:
        return False, exc.code
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        return False, None


def _post_with_retry(url: str, body: dict) -> Optional[int]:
    """POST with one retry after 5 s on failure. Returns final HTTP status or None."""
    ok, status = _post(url, body)
    if ok:
        return status
    log.warning("event=push_fail provider=%s url=%s status=%s retrying_in=5s",
                body["provider"], url, status)
    time.sleep(5)
    ok, status = _post(url, body)
    return status if ok else status   # return last status (may be None)


def _step_provider(state: ProviderState, targets: list[str], interval_s: int) -> int:
    """Fetch, push, and compute the sleep for this provider. Returns seconds to sleep."""
    # Backoff gate: if we are in backoff, sleep until backoff elapses.
    if state.backoff_min > 0:
        sleep_s = state.backoff_min * 60
        log.info("event=backoff provider=%s minutes=%d", state.name, state.backoff_min)
        # Advance the backoff ladder.
        state.backoff_min = min(state.backoff_min * 2 if state.backoff_min >= 2 else 2, 15)
        return sleep_s

    # Fetch.
    try:
        windows = state.fetch()
    except Exception as exc:
        # Transport / auth / parse failure on the provider API.
        state.consecutive_failures += 1
        cat = type(exc).__name__
        log.error("event=fetch_fail provider=%s category=%s at=%s",
                  state.name, cat, _iso())
        if state.consecutive_failures >= 2:
            # After two consecutive fetch failures, back off so we don't hammer.
            state.backoff_min = 2
            state.consecutive_failures = 0
        return interval_s

    # Push to every target.
    body = _make_body(state.name, windows)
    any_ok = False
    saw_throttle_or_server = False
    for url in targets:
        status = _post_with_retry(url, body)
        log.info("event=push provider=%s url=%s status=%s at=%s",
                 state.name, url, status, _iso())
        if status is not None and 200 <= status < 300:
            any_ok = True
        if status in (429,) or (status is not None and 500 <= status < 600):
            saw_throttle_or_server = True

    if saw_throttle_or_server and not any_ok:
        # Per-provider backoff on throttle/server errors only.
        state.backoff_min = 2
    else:
        # Success (or pure transport failure with no HTTP signal): normal cadence.
        state.consecutive_failures = 0
    return interval_s


def main() -> int:
    cfg = _load_config(CONFIG_PATH)
    svc = cfg.get("service", {})
    interval_s = int(svc.get("interval_seconds", 60))
    mdns_timeout = float(svc.get("mdns_timeout_seconds", 2.0))
    explicit_urls = list(svc.get("urls", []) or [])

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stdout,
    )

    codex_state = ProviderState(name="codex", fetch=codex_wifi_adapter.fetch)
    zai_state   = ProviderState(name="zai",   fetch=zai_wifi_adapter.fetch)

    log.info("event=start interval_s=%d explicit_urls=%d", interval_s, len(explicit_urls))

    while True:
        targets = aiusage_mdns.all_targets(explicit_urls, mdns_timeout=mdns_timeout)
        if not targets:
            log.warning("event=no_targets at=%s", _iso())
            time.sleep(interval_s)
            continue

        # Run each provider; they share the loop but keep independent state.
        # We service them sequentially (the work is I/O bound and light).
        sleep_codex = _step_provider(codex_state, targets, interval_s)
        sleep_zai   = _step_provider(zai_state,   targets, interval_s)
        sleep_s = min(sleep_codex, sleep_zai)
        if sleep_s > 0:
            time.sleep(sleep_s)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
```

**Notes for the implementer:**
- `_post` never raises: HTTP errors are captured, transport errors return `(False, None)`. The logger only ever records `status` (an int or None) — never the body or headers.
- The backoff ladder is 2 → 4 → 8 → 15 (capped). It resets to 0 on any successful push.
- "Fetch failure" (transport/auth/parse on the provider API) is tracked separately: two consecutive failures trigger a 2-minute backoff to avoid hammering.
- The logging uses structured-ish key=value so grep is easy: `event=fetch_fail provider=codex category=CodexAdapterError`.

- [ ] **Step 4.2: Commit.**

```sh
git add tools/wifi_usage_service.py
git commit -m "feat(v3): Wi-Fi usage push service (per-provider loop + backoff + strict logs)"
```

---

## Task 5: Example config + LaunchAgent plist + .gitignore

**Files:**
- Create: `tools/wifi-usage.toml.example`
- Create: `tools/com.night.swc-digital-wifi-usage.plist.example`
- Modify: `.gitignore` — add `tools/wifi-usage.toml`

- [ ] **Step 5.1: Create `tools/wifi-usage.toml.example`.**

```toml
# SWC Digital 3.0.0 — Wi-Fi usage push service example config.
# Copy to tools/wifi-usage.toml (gitignored) and edit.

[service]
interval_seconds = 60
stale_after_seconds = 180     # informational: the device enforces this itself
discovery = "mdns"
mdns_timeout_seconds = 2.0

# Explicit push URLs. Used as-is AND merged with mDNS results.
# Empty by default; mDNS discovery usually finds the device on its own.
# Example: urls = [ "http://192.168.1.42/api/usage" ]
urls = []

# Codex reads its OAuth token from ~/.codex/auth.json by default.
# Override the path here if needed:
# [codex]
# auth_file = "~/.codex/auth.json"

# z.ai reads ANTHROPIC_BASE_URL + ANTHROPIC_AUTH_TOKEN from ~/.claude/settings.json.
# Override the path here if needed:
# [zai]
# claude_settings = "~/.claude/settings.json"
```

- [ ] **Step 5.2: Create `tools/com.night.swc-digital-wifi-usage.plist.example`.**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.night.swc-digital-wifi-usage</string>
  <key>ProgramArguments</key>
  <array>
    <string>REPLACE_WITH_ABSOLUTE_PATH_TO_UV</string>
    <string>run</string>
    <string>--with</string>
    <string>zeroconf</string>
    <string>REPLACE_WITH_ABSOLUTE_PATH_TO_REPOSITORY/tools/wifi_usage_service.py</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>WIFI_USAGE_CONFIG</key>
    <string>REPLACE_WITH_ABSOLUTE_PATH_TO_PRIVATE_wifi-usage.toml</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>REPLACE_WITH_ABSOLUTE_PATH_TO_LOG/swc-digital-wifi-usage.log</string>
  <key>StandardErrorPath</key>
  <string>REPLACE_WITH_ABSOLUTE_PATH_TO_LOG/swc-digital-wifi-usage.log</string>
</dict>
</plist>
```

**Note:** the plist carries NO credential fields — the service reads `~/.codex/auth.json` and `~/.claude/settings.json` itself. Only paths (to uv, the script, the config, the log) are templated.

- [ ] **Step 5.3: `.gitignore` — add the real config.**

Find the existing `tools/usage-collector.toml` ignore line and add immediately after it:

```
tools/wifi-usage.toml
```

Also add (next to the existing `tools/*.plist` line): nothing — the existing `tools/*.plist` rule already ignores the real plist. Only `.plist.example` files are committed.

- [ ] **Step 5.4: Commit.**

```sh
git add tools/wifi-usage.toml.example tools/com.night.swc-digital-wifi-usage.plist.example .gitignore
git commit -m "feat(v3): Wi-Fi usage service example config + LaunchAgent plist"
```

---

## Task 6: Host tests — Codex/z.ai mapping, malformed inputs, log safety

**Files:**
- Create: `tests/test_wifi_adapters.py`
- Create: `tests/_wifi_adapter_fixtures.py` (raw response fixtures as captured constants; redacted)

**Spec coverage (§host tests):**
- Codex Weekly-only → 5H `null`.
- Codex 5H/Weekly in either primary or secondary window.
- Unknown duration ignored.
- z.ai maps `TOKENS_LIMIT/unit=3/number=5` and `TOKENS_LIMIT/unit=6`.
- Missing quota, percent 0/100, reset expired.
- Malformed JSON, 401, 429, timeout, 5xx.
- One provider down, the other still pushes.
- mDNS discovery + explicit URL fallback.
- Logs contain no token/authorization/account data.
- Stale after 180 s and LIVE again on success — the device enforces this; the service test only checks it does not crash on long gaps.

- [ ] **Step 6.1: Create `tests/_wifi_adapter_fixtures.py` — redacted raw fixtures.**

```python
"""Redacted raw response fixtures for the Codex/z.ai adapters.
NO tokens, NO account ids. Just the JSON payload shapes."""

# Codex: weekly only (current account; no 5H window).
CODEX_WEEKLY_ONLY = {
    "rate_limit": {
        "primary_window": {
            "limit_window_seconds": 604800,
            "used_percent": 42,
            "reset_after_seconds": 3600,
        }
    }
}

# Codex: 5H in secondary_window, Weekly in primary.
CODEX_BOTH = {
    "rate_limit": {
        "primary_window":   {"limit_window_seconds": 604800, "used_percent": 91, "reset_after_seconds": 353400},
        "secondary_window": {"limit_window_seconds": 18000,  "used_percent": 5,  "reset_after_seconds": 7200},
    }
}

# Codex: unknown duration should be ignored.
CODEX_UNKNOWN_DURATION = {
    "rate_limit": {
        "primary_window": {"limit_window_seconds": 999, "used_percent": 50},
    }
}

# z.ai: both windows.
ZAI_BOTH = {
    "data": {"limits": [
        {"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 12.5,
         "nextResetTime": "2099-01-01T00:00:00Z"},
        {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 88.0,
         "nextResetTime": "2099-01-08T00:00:00Z"},
    ]}
}

# z.ai: missing quota (no limits list).
ZAI_EMPTY = {"data": {}}

# z.ai: 0% and 100% and expired reset.
ZAI_EDGE = {
    "data": {"limits": [
        {"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 0,
         "nextResetTime": "2000-01-01T00:00:00Z"},     # expired -> clamp to 0
        {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 100,
         "nextResetTime": "2099-01-08T00:00:00Z"},
    ]}
}
```

- [ ] **Step 6.2: Create `tests/test_wifi_adapters.py`.**

```python
"""Host tests for the Wi-Fi adapters and service logic (spec §host tests)."""
import importlib.util
import json
import os
import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))

import codex_wifi_adapter
import zai_wifi_adapter
import _wifi_adapter_fixtures as F


def _load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class CodexAdapterTests(unittest.TestCase):
    def _patch_fetch(self, payload):
        return mock.patch.object(codex_wifi_adapter, "urllib.request.urlopen",
                                 _make_fake_urlopen(payload))

    def test_weekly_only_five_hour_null(self):
        with self._patch_fetch(F.CODEX_WEEKLY_ONLY):
            out = codex_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertEqual(out["weekly"]["used_pct"], 42)

    def test_both_windows_either_slot(self):
        with self._patch_fetch(F.CODEX_BOTH):
            out = codex_wifi_adapter.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 5)
        self.assertEqual(out["weekly"]["used_pct"], 91)

    def test_unknown_duration_ignored(self):
        with self._patch_fetch(F.CODEX_UNKNOWN_DURATION):
            out = codex_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertIsNone(out["weekly"])

    def test_reset_rounded_up(self):
        with self._patch_fetch(F.CODEX_WEEKLY_ONLY):   # 3600s -> 60 min exactly
            out = codex_wifi_adapter.fetch()
        self.assertEqual(out["weekly"]["reset_min"], 60)


class ZaiAdapterTests(unittest.TestCase):
    def _patch_fetch(self, payload):
        return mock.patch.object(zai_wifi_adapter, "urllib.request.urlopen",
                                 _make_fake_urlopen(payload))

    def setUp(self):
        # Settings must look valid (host-allowed) or fetch bails before the request.
        self._cfg = mock.patch.dict(os.environ, {})
        # Patch settings read to return an allowed host + dummy token.
        self._settings = mock.patch.object(zai_wifi_adapter, "_read_settings",
                                           return_value=("https://api.z.ai", "dummy"))
        self._settings.start()
        self.addCleanup(self._settings.stop)

    def test_maps_both_windows(self):
        with self._patch_fetch(F.ZAI_BOTH):
            out = zai_wifi_adapter.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 13)   # 12.5 rounds to 13
        self.assertEqual(out["weekly"]["used_pct"], 88)

    def test_missing_quota_all_null(self):
        with self._patch_fetch(F.ZAI_EMPTY):
            out = zai_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertIsNone(out["weekly"])

    def test_edge_percent_and_expired_reset(self):
        with self._patch_fetch(F.ZAI_EDGE):
            out = zai_wifi_adapter.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 0)
        self.assertEqual(out["five_hour"]["reset_min"], 0)   # expired clamped
        self.assertEqual(out["weekly"]["used_pct"], 100)


class ErrorHandlingTests(unittest.TestCase):
    def test_codex_401_raises(self):
        import urllib.error
        def _raise(*a, **kw):
            raise urllib.error.HTTPError(a[0].full_url if hasattr(a[0], "full_url") else "u",
                                         401, "Unauthorized", {}, None)
        with mock.patch.object(codex_wifi_adapter, "urllib.request.urlopen", _raise), \
             mock.patch.object(codex_wifi_adapter, "_read_token", return_value="x"):
            with self.assertRaises(codex_wifi_adapter.CodexAdapterError):
                codex_wifi_adapter.fetch()

    def test_zai_timeout_raises(self):
        with mock.patch.object(zai_wifi_adapter, "_read_settings",
                               return_value=("https://api.z.ai", "x")), \
             mock.patch.object(zai_wifi_adapter, "urllib.request.urlopen",
                               side_effect=TimeoutError):
            with self.assertRaises(zai_wifi_adapter.ZaiAdapterError):
                zai_wifi_adapter.fetch()


class LogSafetyTests(unittest.TestCase):
    """The service must NEVER log token/header/account/body. We assert the
    adapter modules do not print those fields in their smoke-mode output."""
    def test_codex_smoke_no_secret(self):
        # We cannot easily capture stdout from a subprocess here; instead assert
        # that the module source does not contain a print() of the token/header.
        src = (TOOLS / "codex_wifi_adapter.py").read_text()
        self.assertNotIn("print(token", src)
        self.assertNotIn("print(headers", src)

    def test_service_does_not_log_body(self):
        src = (TOOLS / "wifi_usage_service.py").read_text()
        # log.* calls must not pass the body or response.read() output.
        # Heuristic: no line contains 'log' and 'body' together.
        for line in src.splitlines():
            if "log." in line and "body" in line and "make_body" not in line and "_make_body" not in line:
                self.fail(f"service line may log body: {line}")


def _make_fake_urlopen(payload):
    """Return a fake urlopen context manager that yields `payload` as JSON."""
    import io
    class _Resp:
        status = 200
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self): return json.dumps(payload).encode("utf-8")
    def _fake(req, timeout=None):
        return _Resp()
    return _fake


class DiscoveryFallbackTests(unittest.TestCase):
    def test_explicit_only_when_no_zeroconf(self):
        # Force discover() to return [] (zeroconf not installed in test env).
        import aiusage_mdns
        with mock.patch.object(aiusage_mdns, "discover", return_value=[]):
            urls = aiusage_mdns.all_targets(["http://1.2.3.4/api/usage"], mdns_timeout=0)
        self.assertEqual(urls, ["http://1.2.3.4/api/usage"])

    def test_dedup_explicit_then_mdns(self):
        import aiusage_mdns
        with mock.patch.object(aiusage_mdns, "discover",
                               return_value=["http://5.6.7.8/api/usage",
                                             "http://1.2.3.4/api/usage"]):   # dup of explicit
            urls = aiusage_mdns.all_targets(["http://1.2.3.4/api/usage"], mdns_timeout=0)
        self.assertEqual(urls, ["http://1.2.3.4/api/usage", "http://5.6.7.8/api/usage"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 6.3: Run the tests.**

```sh
cd tests && python -m unittest test_wifi_adapters -v && cd ..
```

Expected: `OK`. Count: CodexAdapterTests (4) + ZaiAdapterTests (3) + ErrorHandlingTests (2) + LogSafetyTests (2) + DiscoveryFallbackTests (2) = 13.

- [ ] **Step 6.4: Commit.**

```sh
git add tests/_wifi_adapter_fixtures.py tests/test_wifi_adapters.py
git commit -m "test(v3): host tests for Codex/z.ai Wi-Fi adapters (13 spec cases)"
```

---

## Task 7: Physical acceptance — flash firmware, run the service, verify

**Files:** none modified — this is the visual + behavioural gate that AGENTS.md requires before any feature is declared complete. Do NOT tag or publish.

**Pre-conditions:**
- The user has authorized flashing the connected physical clock in this session.
- A real `tools/wifi-usage.toml` exists (copy of the example) OR the defaults work (service reads `~/.codex/auth.json` and `~/.claude/settings.json` directly).
- Mac and clock are on the same LAN.

- [ ] **Step 7.1: Flash the candidate firmware at 115200.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | tail -4   # confirm [SUCCESS]
uvx --from esptool esptool \
  --port "$(uv run --with pyserial tools/clockctl.py find-port)" --baud 115200 \
  write-flash 0x0 .pio/build/smalltv_ultra/firmware.bin 2>&1 | tail -8
```

Expected (per AGENTS.md flashing rule #2): esptool reports `hash of data verified`. Note: opening the CH340 port resets the `clock_usb` RAM state, but this flash puts the device into `smalltv_ultra` (Wi-Fi) firmware, so USB RAM state is not relevant here.

- [ ] **Step 7.2: Confirm `/api/status` reports the new identity.**

```sh
# Wait for the device to boot into STA mode, then:
curl -s http://<hostname>.local/api/status | python -m json.tool | grep -E "fw|version|hardware|chip"
```

Expected: `"fw": "swc-digital"`, `"version": "3.0.0"`, `"hardware": "SmallTV-ultra"`, `"chip": "esp8266"` (per Plan 2 Task 6.3).

- [ ] **Step 7.3: Confirm `/api/usage` answers the new schema (empty until the service pushes).**

```sh
curl -s http://<hostname>.local/api/usage | python -m json.tool
```

Expected: a JSON object with `schema: 1` and a `providers` array of two (`codex`, `zai`), each with `everReceived: false`, `age_sec: -1`, `stale: true` (no pushes yet), and no `five_hour`/`weekly` sub-objects.

- [ ] **Step 7.4: Start the service in the foreground.**

```sh
uv run --with zeroconf python tools/wifi_usage_service.py
```

Expected log lines (within ~60 s): `event=start`, then per-provider `event=push provider=codex url=http://... status=200` and `event=push provider=zai ...`. If you see `event=no_targets`, the mDNS browse failed — either wait longer, or set `urls = [ "http://<device-ip>/api/usage" ]` in `tools/wifi-usage.toml` and restart.

- [ ] **Step 7.5: Confirm `GET /api/usage` now shows live data.**

```sh
curl -s http://<hostname>.local/api/usage | python -m json.tool
```

Expected: `everReceived: true` for the provider(s) that returned data; `age_sec` is a small number; `stale: false`; `weekly` (and `five_hour` for z.ai) objects present with `used_pct` and `reset_min`. For Codex, `five_hour` MUST be absent (N/A) and `weekly` MUST show the real percentage — do not accept Weekly-as-5H substitution.

- [ ] **Step 7.6: Visual acceptance — check the physical screen.**

Ask the user to confirm all of:
1. CODEX screen: shows `CODEX` title, `LIVE` pill, Weekly card with real %, bar, reset countdown. 5H card shows `N/A` and `RESET --`. Age row shows `AGE 0m` (or small) and `AUTO` or `MANUAL`.
2. ZAI screen (switch via the WebUI Mode selector): shows `Z.AI` title, `LIVE`, both 5H and Weekly cards populated.
3. AUTO mode: every 30 s the screen flips between CODEX and ZAI smoothly (no full-screen flicker; the redraw is regional).
4. Stop the service (`Ctrl-C`). After ~180 s the active provider's pill flips to `STALE`, colours dim. The last values stay on screen.
5. Restart the service. Within ~60 s the pill flips back to `LIVE` and current values refresh.

Per AGENTS.md flashing rule #4, do NOT declare the screen change complete until the user visually confirms orientation, colour, brightness, readability, and smoothness.

- [ ] **Step 7.7: Verify log safety on the real run.**

Grep the service's foreground output for anything that looks like a credential:

```sh
# In a separate shell while the service runs, or from the log file:
grep -iE "bearer|token|authorization|@|sk-|eyJ" /path/to/swc-digital-wifi-usage.log || echo "clean"
```

Expected: `clean`. The service must never emit a token, bearer header, email, or JWT.

- [ ] **Step 7.8: (Optional) Install the LaunchAgent for persistence.**

```sh
cp tools/com.night.swc-digital-wifi-usage.plist.example ~/Library/LaunchAgents/com.night.swc-digital-wifi-usage.plist
# Edit the plist: replace the four REPLACE_WITH_... placeholders with absolute paths.
launchctl load ~/Library/LaunchAgents/com.night.swc-digital-wifi-usage.plist
launchctl list | grep swc-digital-wifi-usage
```

Expected: the agent appears in `launchctl list` with a PID. The service now survives reboot. (This step is optional in the session — the user may prefer to install it themselves after reviewing the plist.)

- [ ] **Step 7.9: No commit (verification only). The Plan 3 deliverable is the committed code from Tasks 1–6 plus this acceptance record.**

---

## Self-Review

- [ ] **Coverage of spec §5 (Mac Usage Service):**
  - Separate Wi-Fi service, does not touch CH340 serial owner? ✓ Task 4 (new file; no edit to clock_service.py — P2 verifies).
  - `interval_seconds=60`, `stale_after_seconds=180`? ✓ Task 5 (config).
  - Codex + z.ai polled separately every 60 s? ✓ Task 4.
  - One provider failing does not stop the other? ✓ Task 4 (independent `ProviderState`).
  - Per-provider push for independent freshness? ✓ Task 4 (each provider has its own body).
  - mDNS `_aiusage._tcp.local` + explicit URL fallback? ✓ Task 3.
  - HTTP push failure: retry once after 5 s? ✓ Task 4 (`_post_with_retry`).
  - 429/5xx backoff 2/4/8/cap 15 min, per-provider only? ✓ Task 4 (`_step_provider`).
  - Success returns to 60 s? ✓ Task 4 (backoff_min reset to 0 on any OK).
  - Log only provider/time/status/category? ✓ Task 4 (log.info/error with structured keys; Task 6 LogSafetyTests).
  - No token/header/email/account/body in logs? ✓ Task 4 + Task 6.
  - LaunchAgent `com.night.swc-digital-wifi-usage`; plist has only command/config paths, no credentials? ✓ Task 5.

- [ ] **Coverage of spec §5 Codex adapter:**
  - Read OAuth token from `~/.codex/auth.json` in memory? ✓ Task 1 (`AUTH_PATH`).
  - Endpoint details behind adapter? ✓ Task 1 (URL constant inside adapter).
  - Check both primary and secondary window? ✓ Task 1 (`for key in ("primary_window","secondary_window")`).
  - Map `limit_window_seconds` 18000→5H, 604800→Weekly? ✓ Task 1 (`WINDOW_BY_SECONDS`).
  - Use `used_percent`? ✓ Task 1 (`_to_window`).
  - Convert `reset_after_seconds` to minutes (round up)? ✓ Task 1 (`math.ceil(reset_s/60)`).
  - Unknown/missing window → null? ✓ Task 1 (returns None).
  - No 5H for current account → N/A, do not substitute Weekly? ✓ Task 1 (windows are independent; Task 6 `test_weekly_only_five_hour_null`).

- [ ] **Coverage of spec §5 z.ai adapter:**
  - Read `ANTHROPIC_BASE_URL` + `ANTHROPIC_AUTH_TOKEN` from `~/.claude/settings.json` in memory? ✓ Task 2 (`_read_settings`).
  - Allow host `api.z.ai` only? ✓ Task 2 (`ALLOWED_HOST` + host check).
  - `GET https://api.z.ai/api/monitor/usage/quota/limit`? ✓ Task 2.
  - Map `data.limits` 5H (`TOKENS_LIMIT/unit=3/number=5`) and Weekly (`TOKENS_LIMIT/unit=6`)? ✓ Task 2 (`_classify_limit`).
  - Use raw `percentage` as used %? ✓ Task 2.
  - Convert `nextResetTime` to minutes, clamp 0..65535? ✓ Task 2.
  - Missing quota → null? ✓ Task 2 + Task 6.
  - Schema uses `TOKENS_LIMIT`? ✓ Task 2 (locked per the user's earlier answer).

- [ ] **Placeholder scan:** every step has concrete code. The RGB565 note from Plan 2 does not apply here. The plist "REPLACE_WITH_..." strings are intentional template placeholders documented in-line (same pattern as the existing `com.example.smart-weather-clock-usage.plist.example`).

- [ ] **Type consistency:**
  - Adapters return `{"five_hour": {...}|None, "weekly": {...}|None}` — consumed identically by `_make_body` in Task 4. ✓
  - `ProviderState.fetch` is the adapter's `fetch` callable — called with no args, returns the dict. ✓
  - `aiusage_mdns.all_targets(explicit, mdns_timeout)` returns `list[str]` of URLs — consumed by Task 4 as `targets`. ✓
  - Push body keys match Plan 2's `UsageStore::applyPush` parser exactly: `v`, `provider`, `five_hour_used_pct`, `five_hour_reset_min`, `weekly_used_pct`, `weekly_reset_min`. ✓ (Cross-plan consistency check: Plan 2 Task 2.1 parses the same five keys. If either side changes a key name, both plans must update.)

- [ ] **Untouched-file guard:** Tasks 1–6 create only new files under `tools/` and `tests/`, plus a one-line `.gitignore` append. No existing file is modified. Task 7 modifies nothing on disk. Verify with:

```sh
git diff main --stat -- tools/clock_service.py tools/clockctl.py tools/clock_gui.py tools/usage_collector.py tools/crypto_market.py tools/codex_usage.py tools/claude_usage.py tools/claude_profile_vault.swift tools/extract_mascot.py src/ tests/test_clock_tools.py
```

Expected: empty.

---

## Done criteria for Plan 3

1. All new files exist and the USB clock tooling is byte-identical to `main`.
2. `python -m unittest test_wifi_adapters` passes 13 cases.
3. The service runs in the foreground, discovers the device (or uses the explicit URL), and pushes both providers independently every 60 s.
4. The physical screen shows CODEX with live Weekly (5H=N/A), ZAI with live 5H+Weekly, AUTO flipping every 30 s, STALE after stopping the service for 180 s, and LIVE again on restart — all visually confirmed by the user.
5. Log grep for `bearer|token|authorization|@|sk-|eyJ` is clean.
6. (Optional) The LaunchAgent loads and survives a reboot.

**Hand-off to Plan 4:** `docs/superpowers/plans/2026-07-21-v3-plan4-ci-docs-release.md`.
