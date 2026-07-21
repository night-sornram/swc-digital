"""Redacted raw response fixtures for the Codex/z.ai adapters.
NO tokens, NO account ids. Just the JSON payload shapes."""

# Codex: weekly only (current account; no 5H window).
CODEX_WEEKLY_ONLY = {
    "rate_limit": {
        "primary_window": {
            "limit_window_seconds": 604800,
            "used_percent": 42,
            "reset_after_seconds": 3600,
        }
    }
}

# Codex: 5H in secondary_window, Weekly in primary.
CODEX_BOTH = {
    "rate_limit": {
        "primary_window":   {"limit_window_seconds": 604800, "used_percent": 91, "reset_after_seconds": 353400},
        "secondary_window": {"limit_window_seconds": 18000,  "used_percent": 5,  "reset_after_seconds": 7200},
    }
}

# Codex: unknown duration should be ignored.
CODEX_UNKNOWN_DURATION = {
    "rate_limit": {
        "primary_window": {"limit_window_seconds": 999, "used_percent": 50},
    }
}

# z.ai: both windows.
ZAI_BOTH = {
    "data": {"limits": [
        {"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 12.5,
         "nextResetTime": "2099-01-01T00:00:00Z"},
        {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 88.0,
         "nextResetTime": "2099-01-08T00:00:00Z"},
    ]}
}

# z.ai: missing quota (no limits list).
ZAI_EMPTY = {"data": {}}

# z.ai: 0% and 100% and expired reset.
ZAI_EDGE = {
    "data": {"limits": [
        {"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 0,
         "nextResetTime": "2000-01-01T00:00:00Z"},     # expired -> clamp to 0
        {"type": "TOKENS_LIMIT", "unit": 6, "percentage": 100,
         "nextResetTime": "2099-01-08T00:00:00Z"},
    ]}
}
