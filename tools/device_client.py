#!/usr/bin/env python3
"""Single Mac-side interface for a paired SmallTV-ultra.

Owns four things:
  1. Pair key storage in macOS Keychain (service com.night.swc-digital.device-<id>).
  2. A urllib opener that does HTTP Digest with username "admin" + the pairkey.
  3. Identity verification: GET /api/identity and confirm the Device ID matches.
  4. The push call (POST /api/usage with one provider's body).

The pair key NEVER crosses the wire except during /api/pair (over the Setup AP
or the trusted home LAN). After pairing, only the resulting H1 lives on the
device; only the pairkey lives in Keychain.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import urllib.error
import urllib.request
from typing import Optional

KEYCHAIN_SERVICE_PREFIX = "com.night.swc-digital.device-"
KEYCHAIN_ACCOUNT = "pairkey"
DIGEST_USERNAME = "admin"
DIGEST_REALM = "swc-digital"

# Crockford Base32 alphabet (no I, L, O, U to avoid 1/I, 0/O confusion).
CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"


class DeviceError(Exception):
    """Raised on any device-client failure."""


# ---- Keychain ---------------------------------------------------------------

def _keychain_service(device_id: str) -> str:
    return KEYCHAIN_SERVICE_PREFIX + device_id


def keychain_set(device_id: str, pairkey: str) -> None:
    """Store the pairkey in the macOS Keychain, keyed by device id."""
    svc = _keychain_service(device_id)
    # Delete any existing entry first (ignore failure if absent).
    subprocess.run(["security", "delete-generic-password", "-s", svc, "-a", KEYCHAIN_ACCOUNT],
                   capture_output=True)
    r = subprocess.run(
        ["security", "add-generic-password", "-s", svc, "-a", KEYCHAIN_ACCOUNT, "-w", pairkey, "-U"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise DeviceError(f"keychain add failed: {r.stderr.strip()}")


def keychain_get(device_id: str) -> str:
    svc = _keychain_service(device_id)
    r = subprocess.run(
        ["security", "find-generic-password", "-s", svc, "-a", KEYCHAIN_ACCOUNT, "-w"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise DeviceError(f"no pairkey in Keychain for device {device_id}")
    return r.stdout.strip()


def keychain_delete(device_id: str) -> None:
    svc = _keychain_service(device_id)
    subprocess.run(["security", "delete-generic-password", "-s", svc, "-a", KEYCHAIN_ACCOUNT],
                   capture_output=True)


# ---- Pairkey generation -----------------------------------------------------

def generate_pairkey(length: int = 16) -> str:
    """16-char Crockford Base32 pairkey. ~80 bits of entropy (32^16 ~ 1.2e24)."""
    import secrets
    return "".join(secrets.choice(CROCKFORD) for _ in range(length))


def compute_h1(pairkey: str) -> str:
    """MD5(user:realm:pairkey) lowercase hex. Mirrors device computeH1."""
    s = f"{DIGEST_USERNAME}:{DIGEST_REALM}:{pairkey}".encode()
    return hashlib.md5(s).hexdigest()


# ---- HTTP -------------------------------------------------------------------

def build_opener(pairkey: str, base_url: str = "") -> urllib.request.OpenerDirector:
    """urllib opener with HTTP Basic auth (admin + pairkey).

    Basic is used (not Digest) because the ESP8266WebServer's Digest impl
    rotates the nonce per challenge, causing browsers to re-prompt every few
    minutes. Basic lets the browser cache credentials for the whole session.

    We add a handler that ALWAYS attaches the Authorization header on the
    first request (rather than waiting for a 401 challenge). The device's
    firmware does not send a proper WWW-Authenticate challenge, so urllib's
    default lazy Basic handler would never learn to authenticate.
    """
    import base64

    class _AlwaysBasic(urllib.request.BaseHandler):
        # urllib's default Basic handler runs at order 490; we run before it
        # so the header is set unconditionally. BaseHandler default is 500.
        handler_order = 100

        def __init__(self, user, pw):
            self._token = base64.b64encode(f"{user}:{pw}".encode()).decode()

        def http_request(self, req):
            req.add_unredirected_header("Authorization", "Basic " + self._token)
            return req

        https_request = http_request

    opener = urllib.request.build_opener(_AlwaysBasic(DIGEST_USERNAME, pairkey))
    return opener


def get_identity(opener, base_url: str, expected_id: Optional[str] = None,
                 timeout: float = 5.0) -> dict:
    """GET /api/identity. Verifies the device's id matches expected_id if given."""
    url = base_url.rstrip("/") + "/api/identity"
    try:
        with opener.open(url, timeout=timeout) as resp:
            data = json.load(resp)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
        raise DeviceError(f"identity request failed: {exc}") from exc
    if expected_id is not None and data.get("id") != expected_id:
        raise DeviceError(f"identity mismatch: expected {expected_id}, got {data.get('id')}")
    return data


def pair(base_url: str, pairkey: str, timeout: float = 5.0) -> None:
    """POST /api/pair with the pairkey. Device computes H1 and persists."""
    url = base_url.rstrip("/") + "/api/pair"
    body = json.dumps({"pairkey": pairkey}).encode()
    req = urllib.request.Request(
        url, data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as exc:
        # 409 = already paired; 400 = bad key; surface the body.
        try:
            body = exc.read().decode()
        except Exception:
            body = ""
        raise DeviceError(f"pair failed: HTTP {exc.code} {body}") from exc
    except (urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
        raise DeviceError(f"pair request failed: {exc}") from exc
    if not data.get("ok"):
        raise DeviceError(f"pair rejected: {data}")


def push_usage(opener, base_url: str, body: dict, timeout: float = 8.0) -> int:
    """POST /api/usage with the provider body. Returns HTTP status. Does NOT
    raise on HTTP errors (caller decides retry/backoff).

    `base_url` may already end in /api/usage (mDNS TXT carries the full path)
    or be just a host root (http://1.2.3.4). Append the path only if missing.
    """
    base = base_url.rstrip("/")
    url = base if base.endswith("/api/usage") else base + "/api/usage"
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.status
    except urllib.error.HTTPError as exc:
        return exc.code
    except (urllib.error.URLError, TimeoutError, OSError):
        return -1   # transport failure (no HTTP status)


if __name__ == "__main__":
    print("device_client: use wifi_usage_service.py pair|run|recover", file=sys.stderr)
    sys.exit(2)
