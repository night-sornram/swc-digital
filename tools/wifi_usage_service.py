#!/usr/bin/env python3
"""SWC Digital 3.1 Wi-Fi usage service — Mac-side CLI.

Subcommands:
  pair     Generate a pairkey, store it in Keychain, POST it to the device.
  run      Daemon: poll providers + push to the paired device every 60s.
  recover  Re-pair over a Setup AP after Wi-Fi change or lost key.

Legacy compat: calling with NO subcommand runs `run` (so the existing
LaunchAgent plist keeps working until the user updates it).
"""
from __future__ import annotations

import argparse
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import codex_wifi_adapter
import zai_wifi_adapter
import system_stats_adapter
import aiusage_mdns
import device_client

CONFIG_PATH = Path(os.environ.get("WIFI_USAGE_CONFIG",
                                  Path(__file__).resolve().parent / "wifi-usage.toml"))
log = logging.getLogger("wifi-usage")


def _iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _load_config(path: Path) -> dict:
    with path.open("rb") as h:
        return tomllib.load(h)


# ---- pair ------------------------------------------------------------------

def cmd_pair(args) -> int:
    """`pair --url http://smalltv-c089.local`"""
    base_url = args.url
    if not base_url:
        print("pair: --url is required (e.g. http://smalltv-c089.local)", file=sys.stderr)
        return 2
    # Probe identity first so we know which device we are pairing with and
    # which Keychain service name to use.
    import urllib.request
    noauth = urllib.request.build_opener()
    try:
        ident = device_client.get_identity(noauth, base_url)
    except device_client.DeviceError as exc:
        print(f"pair: cannot reach device: {exc}", file=sys.stderr)
        return 2
    device_id = ident["id"]
    pairkey = device_client.generate_pairkey()
    try:
        device_client.pair(base_url, pairkey)
    except device_client.DeviceError as exc:
        print(f"pair: device rejected pair: {exc}", file=sys.stderr)
        return 2
    device_client.keychain_set(device_id, pairkey)
    # Update private config so `run` knows which device to target.
    _write_device_id_to_config(device_id)
    print(f"Paired device {device_id}.")
    print(f"Pairkey (SAVE in your password manager — shown once): {pairkey}")
    return 0


def cmd_recover(args) -> int:
    """`recover --url http://192.168.4.1`  (Setup AP only)

    Generates a fresh pairkey, stores it in Keychain, and pairs over the AP.
    Use after moving Wi-Fi or losing the key. Requires the device to be in
    Setup AP mode (long-press reset / factory reset).
    """
    # Recovery is structurally identical to pair (the device's /api/pair is
    # open in AP mode). The separate command name documents intent and lets
    # the LaunchAgent grow a "recover weekly" check later.
    return cmd_pair(args)


def _write_device_id_to_config(device_id: str) -> None:
    cfg_path = Path(os.environ.get("WIFI_USAGE_CONFIG", CONFIG_PATH))
    if not cfg_path.exists():
        return
    # Append/replace [device] id. Simple line-based edit (tomllib has no writer).
    lines = cfg_path.read_text().splitlines()
    out = []
    in_device = False
    wrote = False
    for line in lines:
        s = line.strip()
        if s.startswith("[device]"):
            in_device = True; out.append(line); continue
        if s.startswith("[") and in_device:
            in_device = False
        if in_device and s.startswith("id"):
            out.append(f'id = "{device_id}"'); wrote = True; continue
        out.append(line)
    if not wrote:
        out.append("")
        out.append("[device]")
        out.append(f'id = "{device_id}"')
    cfg_path.write_text("\n".join(out) + "\n")


# ---- run (the existing main loop) ------------------------------------------

@dataclass
class ProviderState:
    name: str
    fetch: object
    backoff_min: int = 0
    consecutive_failures: int = 0


def _make_body(provider_token: str, windows: dict) -> dict:
    def _w(w, key):
        if w is None: return None
        return w.get(key)
    fh = windows.get("five_hour")
    wk = windows.get("weekly")
    body = {
        "v": 1,
        "provider": provider_token,
        "five_hour_used_pct":  _w(fh, "used_pct"),
        "five_hour_reset_min": _w(fh, "reset_min"),
        "weekly_used_pct":     _w(wk, "used_pct"),
        "weekly_reset_min":    _w(wk, "reset_min"),
    }
    # SYSTEM provider carries an optional 3rd metric (SSD) that the AI
    # providers never set. device_client accepts it iff present.
    if windows.get("extra_pct") is not None:
        body["extra_pct"] = windows["extra_pct"]
    return body


def _resolve_targets(cfg: dict) -> list[tuple[str, str]]:
    """Return [(url, id), ...] from mDNS + explicit URLs, filtered by config id."""
    svc = cfg.get("service", {})
    explicit = list(svc.get("urls", []) or [])
    mdns_timeout = float(svc.get("mdns_timeout_seconds", 2.0))
    dev_id = (cfg.get("device", {}) or {}).get("id") or None
    return aiusage_mdns.all_targets(explicit, mdns_timeout=mdns_timeout, device_id=dev_id)


def cmd_run(args) -> int:
    cfg = _load_config(Path(args.config) if args.config else CONFIG_PATH)
    svc = cfg.get("service", {})
    interval_s = int(svc.get("interval_seconds", 60))
    dev_cfg = cfg.get("device", {}) or {}
    dev_id = dev_cfg.get("id")

    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", stream=sys.stdout)
    log.info("event=start interval_s=%d device_id=%s", interval_s, dev_id or "<any>")

    if not dev_id:
        log.warning("event=no_device_id hint=run 'wifi_usage_service.py pair' first")

    codex_state  = ProviderState(name="codex",  fetch=codex_wifi_adapter.fetch)
    zai_state    = ProviderState(name="zai",    fetch=zai_wifi_adapter.fetch)
    system_state = ProviderState(name="system", fetch=system_stats_adapter.fetch)

    # Build the Digest opener once we have a pairkey. Cached per device id.
    opener_cache: dict[str, object] = {}
    def opener_for(url_id_pair):
        url, _id = url_id_pair
        # For each target we must resolve the device id, then look up its pairkey.
        # If the mDNS-discovered id is present, use it; else probe /api/identity.
        target_id = _id
        if not target_id:
            try:
                noauth = urllib.request.build_opener()
                ident = device_client.get_identity(noauth, url, expected_id=dev_id)
                target_id = ident["id"]
            except device_client.DeviceError as exc:
                log.error("event=identity_fail url=%s error=%s", url, exc)
                return None
        if target_id not in opener_cache:
            try:
                pairkey = device_client.keychain_get(target_id)
            except device_client.DeviceError:
                log.error("event=no_pairkey device=%s hint=run 'pair'", target_id)
                return None
            opener_cache[target_id] = device_client.build_opener(pairkey, url)
        return opener_cache[target_id]

    while True:
        targets = _resolve_targets(cfg)
        if not targets:
            log.warning("event=no_targets at=%s", _iso())
            if getattr(args, "once", False):
                return 2
            time.sleep(interval_s); continue
        for state in (codex_state, zai_state, system_state):
            _step_provider(state, targets, interval_s, opener_for, dev_id)
        if getattr(args, "once", False):
            return 0
        time.sleep(interval_s)


def _step_provider(state, targets, interval_s, opener_for, expected_id) -> None:
    if state.backoff_min > 0:
        sleep_s = state.backoff_min * 60
        log.info("event=backoff provider=%s minutes=%d", state.name, state.backoff_min)
        state.backoff_min = min(state.backoff_min * 2 if state.backoff_min >= 2 else 2, 15)
        time.sleep(sleep_s); return
    try:
        windows = state.fetch()
    except Exception as exc:
        state.consecutive_failures += 1
        log.error("event=fetch_fail provider=%s category=%s", state.name, type(exc).__name__)
        if state.consecutive_failures >= 2:
            state.backoff_min = 2; state.consecutive_failures = 0
        return
    body = _make_body(state.name, windows)
    saw_auth_fail = False
    any_ok = False
    for tgt in targets:
        opener = opener_for(tgt)
        if not opener: continue
        status = device_client.push_usage(opener, tgt[0], body)
        log.info("event=push provider=%s url=%s status=%s", state.name, tgt[0], status)
        if status in (200, 204): any_ok = True
        elif status == 401 or status == 403:
            saw_auth_fail = True
            log.error("event=auth_failed provider=%s url=%s — pausing 15min", state.name, tgt[0])
            break   # do NOT retry 401/403 after 5s like network failure
    if saw_auth_fail:
        state.backoff_min = 15
    elif not any_ok:
        # network failure: brief retry handled by push_usage already
        pass
    else:
        state.consecutive_failures = 0


# ---- main ------------------------------------------------------------------

def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd")

    p_pair = sub.add_parser("pair", help="pair with a device over its Setup AP or trusted LAN")
    p_pair.add_argument("--url", required=False, help="device base URL, e.g. http://smalltv-c089.local")
    p_pair.set_defaults(func=cmd_pair)

    p_run = sub.add_parser("run", help="daemon: poll providers + push every 60s")
    p_run.add_argument("--config", help="path to wifi-usage.toml")
    p_run.add_argument("--once", action="store_true", help="one iteration then exit (testing)")
    p_run.set_defaults(func=cmd_run)

    p_rec = sub.add_parser("recover", help="re-pair over a Setup AP after Wi-Fi change")
    p_rec.add_argument("--url", required=True, help="device AP URL, e.g. http://192.168.4.1")
    p_rec.set_defaults(func=cmd_recover)

    args = p.parse_args(argv)
    if not args.cmd:
        # Legacy compat: bare invocation runs the daemon. The existing
        # LaunchAgent plist (com.night.swc-digital-wifi-usage) uses this.
        args = p_run.parse_args([])
        args.cmd = "run"
    return args.func(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
