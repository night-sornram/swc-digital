"""Mirror of src/features/usage/UsageStore.cpp push validation.
Kept in sync by tests/test_usage_store.py. If you change the C++, change this too.

Routes (must match UsageApi.cpp):
  codex, zai, system, vitals, weather

Per-provider field meanings (the struct reuses slots; the renderer interprets):
  five_hour_used_pct: codex/zai=5H%, system=CPU%, vitals=CPU%, weather=temp°C
  weekly_used_pct:    codex/zai=Weekly%, system=RAM%, vitals=RAM%, weather=AQI index
  extra_pct:          system=SSD%, vitals=SSD%, weather=temp_max°C
  temp_c:             vitals=Mac temp (-127..127, optional)
  battery_pct:        vitals=battery (0..100, optional)
  uptime_min:         vitals=uptime (0..65535, optional)
  weather_code:       weather=WMO code (0..99, optional)
  aqi_pm25:           weather=PM2.5 µg/m³ (0..255, optional)
  temp_min:           weather=daily low °C (-127..127, optional)
"""
from typing import Optional

# Valid provider routes (must match UsageApi.cpp's routing table).
_VALID_ROUTES = ("codex", "zai", "system", "vitals", "weather")

# Optional fields and their validation: (key, min, max)
# None bounds = no range check (only type check).
_OPTIONAL_INT_FIELDS = {
    "temp_c":       (-127, 127),
    "battery_pct":  (0, 100),
    "uptime_min":   (0, 65535),
    "weather_code": (0, 99),
    "aqi_pm25":     (0, 255),
    "temp_min":     (-127, 127),
    "temp_max":     (-127, 127),
}


def _is_int(v) -> bool:
    """Accept int but NOT bool (Python bool is a subclass of int)."""
    return isinstance(v, int) and not isinstance(v, bool)


def validate_push(provider_route: str, body: dict) -> bool:
    """Return True iff a POST /api/usage with this body would be accepted.

    Mirrors the full accept path: UsageApi.cpp's route filter plus
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
    # Two window slots (five_hour / weekly): pct 0..100, reset_min 0..65535.
    for pk, rk in (("five_hour_used_pct", "five_hour_reset_min"),
                   ("weekly_used_pct",   "weekly_reset_min")):
        pct = body.get(pk)
        if pct is not None:
            if not _is_int(pct):
                return False
            if pct < 0 or pct > 100:
                return False
            saw_pct = True
        rst = body.get(rk)
        if rst is not None:
            if not _is_int(rst):
                return False
            if rst < 0 or rst > 65535:
                return False
    # extra_pct: optional, 0..100 (system SSD, vitals SSD, weather temp_max).
    extra = body.get("extra_pct")
    if extra is not None:
        if not _is_int(extra) or extra < 0 or extra > 100:
            return False
        saw_pct = True
    # New optional fields with per-field ranges.
    for key, (lo, hi) in _OPTIONAL_INT_FIELDS.items():
        v = body.get(key)
        if v is not None:
            if not _is_int(v):
                return False
            if v < lo or v > hi:
                return False
            saw_pct = True
    return saw_pct
