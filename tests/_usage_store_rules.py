"""Mirror of src/features/usage/UsageStore.cpp push validation.
Kept in sync by tests/test_usage_store.py. If you change the C++, change this too."""
from typing import Optional

# Valid provider routes (must match UsageApi.cpp's routing table).
_VALID_ROUTES = ("codex", "zai")

def validate_push(provider_route: str, body: dict) -> bool:
    """Return True iff a POST /api/usage with this body would be accepted.

    Mirrors the full accept path: UsageApi.cpp's route filter (only codex/zai
    are real routes — anything else gets a 400 before applyPush runs) plus
    UsageStore::applyPush's strict body validation."""
    if provider_route not in _VALID_ROUTES:
        return False
    if body.get("v") != 1:
        return False
    tok = body.get("provider")
    if not isinstance(tok, str):
        return False
    if tok.lower() != provider_route:
        return False
    saw_pct = False
    for pk, rk in (("five_hour_used_pct", "five_hour_reset_min"),
                   ("weekly_used_pct",   "weekly_reset_min")):
        pct = body.get(pk)
        if pct is not None:
            if not isinstance(pct, int) or isinstance(pct, bool):
                return False
            if pct < 0 or pct > 100:
                return False
            saw_pct = True
        rst = body.get(rk)
        if rst is not None:
            if not isinstance(rst, int) or isinstance(rst, bool):
                return False
            if rst < 0 or rst > 65535:
                return False
    return saw_pct
