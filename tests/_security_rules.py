"""Mirror of src/Security.cpp rules. Kept in sync by test_security.py.
If you change the C++ identity shape, change this too."""

# Required keys + types in GET /api/identity response.
IDENTITY_REQUIRED = {
    "id":      str,   # 8 lowercase hex chars
    "fw":      str,   # "swc-digital"
    "version": str,   # semver
    "paired":  bool,
    "mode":    str,   # "ap" | "sta"
}

def valid_device_id(s: str) -> bool:
    """Device ID is 8 lowercase hex chars (ESP.getChipId() formatted %08x)."""
    return (
        isinstance(s, str)
        and len(s) == 8
        and all(c in "0123456789abcdef" for c in s)
    )

def valid_identity(d: dict) -> bool:
    if not isinstance(d, dict):
        return False
    for k, t in IDENTITY_REQUIRED.items():
        if k not in d or not isinstance(d[k], t):
            return False
    if not valid_device_id(d["id"]):
        return False
    if d["mode"] not in ("ap", "sta"):
        return False
    return True

import hashlib

REALM = "swc-digital"
USER  = "admin"

def compute_h1(pairkey: str) -> str:
    """Mirror of Security.cpp computeH1(). MD5(user:realm:pairkey) lowercase hex."""
    return hashlib.md5(f"{USER}:{REALM}:{pairkey}".encode()).hexdigest()

ROUTES = ["identity", "pair", "root", "config_get", "config_post",
          "status", "scan", "export", "import", "reboot", "factory",
          "checkupdate", "selfupdate", "update", "usage_get",
          "usage_post", "captive_probe"]

def route_status(device_state, mode, route, has_auth):
    """Mirror of Plan 2 Task 5 logic. device_state in {unpaired,paired},
    mode in {ap,sta}, has_auth in {True,False}."""
    if route in ("identity", "captive_probe"):
        return 200
    if route == "pair":
        if mode != "ap":                return 404
        if device_state != "unpaired":  return 409
        return 200
    if device_state == "unpaired":      return 200
    if has_auth:                        return 200
    return 401
