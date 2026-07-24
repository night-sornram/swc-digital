#!/usr/bin/env python3
"""SWC Digital 3.1 Wi-Fi usage service — Mac-side CLI.

Subcommands:
  pair     Generate a pairkey, store it in Keychain, POST it to the device.
  run      Daemon: poll providers + push to the paired device every 60s.
  recover  Re-pair over a Setup AP after Wi-Fi change or lost key.
  install  Register the LaunchAgent so the service auto-starts at login and
           survives reboots. Writes the plist with correct absolute paths,
           loads it, and verifies the job is running. Idempotent.
  setup    Guided one-shot setup for a fresh Mac: deps → config → pair →
           install → verify. The easiest way to get a new machine running.
  uninstall  Stop the service and remove the LaunchAgent.

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
import vitals_adapter
import weather_adapter
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


# ---- install (register the LaunchAgent) ------------------------------------

LABEL = "com.night.swc-digital-wifi-usage"


def _resolve_uv() -> str:
    """Find the uv binary. Prefer `which uv`, fall back to common paths."""
    import shutil
    found = shutil.which("uv")
    if found:
        return found
    for candidate in ("/opt/homebrew/bin/uv", "/usr/local/bin/uv",
                      os.path.expanduser("~/.local/bin/uv")):
        if os.path.exists(candidate):
            return candidate
    raise RuntimeError(
        "uv not found on PATH or in common locations. "
        "Install uv (https://docs.astral.sh/uv/) or pass --uv-bin.")


def _plist_contents(uv_bin: str, script: str, config: str, log_path: str) -> str:
    """The LaunchAgent plist body. Keep RunAtLoad + KeepAlive so the service
    starts at login and auto-restarts if the process ever crashes."""
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>{LABEL}</string>
  <key>ProgramArguments</key>
  <array>
    <string>{uv_bin}</string>
    <string>run</string>
    <string>--python</string>
    <string>3.11</string>
    <string>--with</string>
    <string>zeroconf</string>
    <string>--with</string>
    <string>psutil</string>
    <string>{script}</string>
    <string>run</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>WIFI_USAGE_CONFIG</key>
    <string>{config}</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>{log_path}</string>
  <key>StandardErrorPath</key>
  <string>{log_path}</string>
</dict>
</plist>
"""


def cmd_install(args) -> int:
    """Register the LaunchAgent: write the plist, load it, verify.

    Idempotent and fresh-Mac friendly: auto-creates the config from the
    example if missing, and offers to run `pair` if the device is unpaired.
    """
    import subprocess
    repo = Path(__file__).resolve().parent
    config = Path(os.environ.get("WIFI_USAGE_CONFIG", repo / "wifi-usage.toml"))
    script = repo / "wifi_usage_service.py"
    uv_bin = args.uv_bin or _resolve_uv()
    plist_path = Path("~/Library/LaunchAgents").expanduser() / f"{LABEL}.plist"
    log_path = Path("~/Library/Logs/swc-digital-wifi-usage.log").expanduser()

    # Auto-create config from the example if a fresh clone has none yet.
    if not config.exists():
        example = repo / "wifi-usage.toml.example"
        if example.exists():
            config.write_text(example.read_text())
            print(f"created {config} from {example.name} (edit if you want "
                  "non-default weather coords).")
        else:
            print(f"install: config not found at {config} and no example to "
                  "copy from.", file=sys.stderr)
            return 2

    # Is this Mac paired? If --url was given, pair non-interactively; otherwise
    # just warn (the guided `setup` command handles interactive pairing, and
    # prompting from inside install causes stdin-order bugs under launchd).
    dev_id = _read_device_id(config)
    paired = False
    if dev_id:
        try:
            import device_client as dc
            dc.keychain_get(dev_id)   # raises if missing
            paired = True
        except Exception:
            pass   # id present but no key — fall through
    if not paired:
        if getattr(args, "url", None):
            ns = argparse.Namespace(url=args.url)
            if cmd_pair(ns) != 0:
                print("install: pairing failed — the service will be registered "
                      "but cannot push until you pair.", file=sys.stderr)
        else:
            print("install: device not yet paired. The service is registered "
                  "but will not push until you pair:", file=sys.stderr)
            print("  run `wifi_usage_service.py setup` (guided), or "
                  "`pair --url <url>` then restart.", file=sys.stderr)

    plist_dir = plist_path.parent
    plist_dir.mkdir(parents=True, exist_ok=True)

    # Unload any existing job first so the reload is clean (idempotent).
    subprocess.run(["launchctl", "unload", str(plist_path)],
                   capture_output=True)
    plist_path.write_text(_plist_contents(uv_bin, str(script),
                                          str(config), str(log_path)))
    print(f"wrote {plist_path}")

    res = subprocess.run(["launchctl", "load", str(plist_path)],
                         capture_output=True, text=True)
    if res.returncode != 0:
        print(f"install: launchctl load failed:\n{res.stderr}", file=sys.stderr)
        return 2

    # Verify the job landed and is running.
    import time as _t
    _t.sleep(1.5)
    lst = subprocess.run(["launchctl", "list"], capture_output=True, text=True)
    line = next((l for l in lst.stdout.splitlines() if LABEL in l), None)
    if not line:
        print("install: job not found in launchctl list after load.",
              file=sys.stderr)
        return 2
    # Columns: PID  Status  Label. PID != "-" means running.
    parts = line.split()
    pid = parts[0] if parts else "-"
    print(f"registered: {LABEL}")
    print(f"  status: {'running (pid ' + pid + ')' if pid != '-' else 'loaded (will start momentarily)'}")
    print(f"  config: {config}")
    print(f"  log:    {log_path}")
    print(f"  uv:     {uv_bin}")
    print()
    print("Done. The service auto-starts at login and restarts if it crashes.")
    print(f"Tail logs: tail -f {log_path}")
    return 0


# ---- interactive helpers (used by install + setup) -------------------------

def _prompt(question: str, default: str = "") -> str:
    """Read a line from stdin with an optional default shown in brackets."""
    suffix = f" [{default}]" if default else ""
    try:
        val = input(f"{question}{suffix}: ").strip()
    except EOFError:
        val = ""
    return val or default


def _confirm(question: str) -> bool:
    """Yes/no prompt. Empty input = yes (keeps guided flow moving)."""
    try:
        ans = input(f"{question} [Y/n] ").strip().lower()
    except EOFError:
        ans = ""
    return ans in ("", "y", "yes")


def cmd_setup(args) -> int:
    """One-shot guided setup for a fresh Mac: deps → config → pair → install → verify."""
    repo = Path(__file__).resolve().parent
    print("=" * 60)
    print("  SWC Digital — fresh Mac setup")
    print("=" * 60)

    # 1. Dependencies.
    print("\n[1/4] Checking dependencies...")
    try:
        uv_bin = _resolve_uv()
        print(f"  ✓ uv found: {uv_bin}")
    except RuntimeError as exc:
        print(f"  ✗ {exc}", file=sys.stderr)
        return 2
    import shutil
    missing = [d for d in ("zeroconf", "psutil")
               if shutil.which("uv") and _py_dep_missing(d)]
    # zeroconf/psutil are pulled by `uv run --with`, so we don't require a
    # global install; just note that the service provides them.
    print("  ✓ zeroconf + psutil provided via `uv run --with` at runtime")

    # 2. Config.
    print("\n[2/4] Config...")
    config = Path(os.environ.get("WIFI_USAGE_CONFIG", repo / "wifi-usage.toml"))
    if not config.exists():
        example = repo / "wifi-usage.toml.example"
        config.write_text(example.read_text())
        print(f"  ✓ created {config} from example")
    else:
        print(f"  ✓ config exists: {config}")

    # 3. Pair (unless already paired).
    print("\n[3/4] Pairing...")
    dev_id = _read_device_id(config)
    paired = False
    if dev_id:
        try:
            import device_client as dc
            dc.keychain_get(dev_id)
            paired = True
            print(f"  ✓ already paired with device {dev_id}")
        except Exception:
            pass
    if not paired:
        url = _prompt("Device URL (e.g. http://192.168.4.1)",
                      default="http://192.168.4.1")
        ns = argparse.Namespace(url=url)
        if cmd_pair(ns) != 0:
            print("  ✗ pairing failed. Fix the issue and re-run `setup`.",
                  file=sys.stderr)
            return 2

    # 4. Install the LaunchAgent.
    print("\n[4/4] Installing LaunchAgent...")
    ns = argparse.Namespace(uv_bin=uv_bin, url=None)
    if cmd_install(ns) != 0:
        print("  ✗ install failed.", file=sys.stderr)
        return 2

    print("\n" + "=" * 60)
    print("  Setup complete! The service is running and will auto-start at login.")
    print(f"  Tail logs: tail -f ~/Library/Logs/swc-digital-wifi-usage.log")
    print("=" * 60)
    return 0


def _py_dep_missing(dep: str) -> bool:
    """True if a Python package can't be imported in the current env."""
    try:
        __import__(dep)
        return False
    except ImportError:
        return True


def _read_device_id(config: Path) -> Optional[str]:
    """Read [device] id from the config without requiring all deps."""
    try:
        with open(config, "rb") as f:
            cfg = tomllib.load(f)
        return (cfg.get("device", {}) or {}).get("id")
    except Exception:
        return None


def cmd_uninstall(args) -> int:
    """Stop the service and remove the LaunchAgent plist."""
    import subprocess
    plist_path = Path("~/Library/LaunchAgents").expanduser() / f"{LABEL}.plist"
    if plist_path.exists():
        subprocess.run(["launchctl", "unload", str(plist_path)],
                       capture_output=True)
        plist_path.unlink()
        print(f"removed {plist_path}")
    else:
        print(f"uninstall: no plist at {plist_path} (nothing to remove)")
    print(f"stopped {LABEL}. Re-run `install` to start again.")
    return 0


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
    # SYSTEM/VITALS: optional 3rd metric (SSD).
    if windows.get("extra_pct") is not None:
        body["extra_pct"] = windows["extra_pct"]
    # VITALS: temp_c, battery_pct, uptime_min.
    for k in ("temp_c", "battery_pct", "uptime_min"):
        v = windows.get(k)
        if v is not None:
            body[k] = v
    # WEATHER: weather_code, temp_min, temp_max, aqi_pm25.
    for k in ("weather_code", "temp_min", "temp_max", "aqi_pm25"):
        v = windows.get(k)
        if v is not None:
            body[k] = v
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

    # Weather fetches every 600 s (10 min) — changes slowly, saves API calls.
    weather_lat = float((cfg.get("weather", {}) or {}).get("lat", 13.7563))
    weather_lon = float((cfg.get("weather", {}) or {}).get("lon", 100.5018))
    weather_last_fetch = [0.0]   # mutable holder for the closure

    def weather_fetch():
        # Throttle: only actually fetch every 600 s.
        now = time.time()
        if now - weather_last_fetch[0] < 600:
            return None   # signal "skip this push" — handled by _step_provider
        weather_last_fetch[0] = now
        return weather_adapter.fetch(weather_lat, weather_lon)

    vitals_state  = ProviderState(name="vitals",  fetch=vitals_adapter.fetch)
    weather_state = ProviderState(name="weather", fetch=weather_fetch)

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
        for state in (codex_state, zai_state, system_state,
                      vitals_state, weather_state):
            _step_provider(state, targets, interval_s, opener_for, dev_id)
        if getattr(args, "once", False):
            return 0
        time.sleep(interval_s)


def _step_provider(state, targets, interval_s, opener_for, expected_id) -> None:
    if state.backoff_min > 0:
        # Non-blocking backoff: skip this provider for the cycle but do NOT
        # sleep — that would stall the whole loop and starve every other
        # provider (one codex backoff must not freeze system/vitals/zai).
        # `backoff_min` here counts skipped cycles, not wall-clock minutes;
        # the name is kept for log-line continuity with prior versions.
        log.info("event=backoff provider=%s minutes=%d", state.name, state.backoff_min)
        state.backoff_min -= 1
        return
    try:
        windows = state.fetch()
    except Exception as exc:
        state.consecutive_failures += 1
        log.error("event=fetch_fail provider=%s category=%s", state.name, type(exc).__name__)
        if state.consecutive_failures >= 2:
            state.backoff_min = 2; state.consecutive_failures = 0
        return
    if windows is None:
        return   # throttled skip (weather 600 s cadence)
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

    p_inst = sub.add_parser("install",
                            help="register the LaunchAgent (auto-start at login, "
                                 "survive reboots). Idempotent. Auto-creates "
                                 "config + offers to pair on a fresh Mac.")
    p_inst.add_argument("--uv-bin", help="path to uv binary (default: auto-detect)")
    p_inst.add_argument("--url", help="device URL to pair with if unpaired "
                                      "(default: prompt interactively)")
    p_inst.set_defaults(func=cmd_install)

    p_uninst = sub.add_parser("uninstall", help="stop the service and remove the LaunchAgent")
    p_uninst.set_defaults(func=cmd_uninstall)

    p_setup = sub.add_parser("setup",
                             help="guided one-shot setup for a fresh Mac: deps → "
                                  "config → pair → install → verify.")
    p_setup.set_defaults(func=cmd_setup)

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
