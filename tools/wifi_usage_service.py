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
