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
