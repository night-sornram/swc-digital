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
