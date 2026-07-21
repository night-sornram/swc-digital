#!/usr/bin/env python3
"""mDNS discovery for _aiusage._tcp (SWC Digital 3.1).

Returns a list of (push URL, Device ID) tuples discovered on the local network
within a short browse window. The Device ID is the TXT record `id` advertised
by the device; the URL is built from the first address + the TXT `path`.
If `zeroconf` is not installed or nothing is found, callers fall back to their
explicit URL list.

Never logs credentials; this module only performs local-network discovery.
"""
from __future__ import annotations

import time
from typing import Iterable

SERVICE_TYPE = "_aiusage._tcp.local."


def discover(timeout: float = 2.0) -> list[tuple[str, str]]:
    """Return [(url, device_id), ...] discovered via mDNS within `timeout` seconds.

    Each entry pairs the push URL (e.g. http://192.168.1.42/api/usage) with the
    Device ID advertised in the TXT record `id` (8 lowercase hex chars), or "" if
    the device did not advertise one.
    """
    try:
        from zeroconf import IPVersion, ServiceBrowser, Zeroconf
    except ImportError:
        return []

    found: list[tuple[str, str]] = []

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
            try:
                path = info.properties.get(b"path", b"/api/usage").decode("ascii", "replace")
            except Exception:
                path = "/api/usage"
            try:
                dev_id = info.properties.get(b"id", b"").decode("ascii", "replace")
            except Exception:
                dev_id = ""
            found.append((f"http://{addresses[0]}{path}", dev_id))

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, SERVICE_TYPE, _Listener())
        time.sleep(timeout)
    finally:
        zc.close()
    # de-dup by url, preserve order
    seen = set(); out = []
    for u, d in found:
        if u not in seen:
            seen.add(u); out.append((u, d))
    return out


def all_targets(explicit: Iterable[str], mdns_timeout: float = 2.0,
                device_id: str | None = None) -> list[tuple[str, str]]:
    """Return [(url, id), ...] of reachable devices.

    Ordering: explicit URLs first (id unknown — caller verifies via
    /api/identity), then mDNS-discovered URLs. If `device_id` is given, return
    ONLY devices whose advertised id matches. If multiple devices share the
    same id (a misconfigured LAN), fail closed: return [].
    """
    out: list[tuple[str, str]] = []
    # Explicit URLs: their id is NOT known from TXT (the user typed the URL).
    # The caller (device_client.get_identity) verifies id against expectation
    # before pushing, so here we attach id="" and let the caller filter.
    for url in explicit:
        if url and url not in [u for (u, _) in out]:
            out.append((url, ""))
    # mDNS: each hit carries an `id` TXT record.
    for url, dev_id in discover(mdns_timeout):
        if device_id and dev_id != device_id:
            continue   # not our device
        # Fail-closed: duplicate device id.
        if dev_id and dev_id in [d for (_, d) in out]:
            return []
        if url not in [u for (u, _) in out]:
            out.append((url, dev_id))
    return out


if __name__ == "__main__":
    import json
    print(json.dumps(discover(timeout=3.0)))
