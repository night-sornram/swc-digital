"""Host tests for the /api/usage push validation (spec §host tests).
These test the Python mirror in _usage_store_rules.py. The C++ in
src/features/usage/UsageStore.cpp MUST match these rules."""
import unittest
from _usage_store_rules import validate_push

class PushValidationTests(unittest.TestCase):
    def test_codex_weekly_only(self):
        b = {"v":1,"provider":"codex","five_hour_used_pct":None,
             "five_hour_reset_min":None,"weekly_used_pct":42,"weekly_reset_min":600}
        self.assertTrue(validate_push("codex", b))

    def test_missing_window_is_null(self):
        # 5H absent -> accepted, marked N/A on device
        b = {"v":1,"provider":"codex","weekly_used_pct":42,"weekly_reset_min":600}
        self.assertTrue(validate_push("codex", b))

    def test_zai_full(self):
        b = {"v":1,"provider":"zai","five_hour_used_pct":5,
             "five_hour_reset_min":120,"weekly_used_pct":91,"weekly_reset_min":5890}
        self.assertTrue(validate_push("zai", b))

    def test_bad_version(self):
        b = {"v":2,"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("codex", b))

    def test_missing_version(self):
        b = {"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("codex", b))

    def test_bad_provider_token(self):
        b = {"v":1,"provider":"claude","weekly_used_pct":42}
        self.assertFalse(validate_push("claude", b))

    def test_provider_token_mismatch(self):
        # body says codex, route says zai -> reject (state untouched)
        b = {"v":1,"provider":"codex","weekly_used_pct":42}
        self.assertFalse(validate_push("zai", b))

    def test_no_window(self):
        b = {"v":1,"provider":"codex"}
        self.assertFalse(validate_push("codex", b))

    def test_pct_out_of_range(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":101}
        self.assertFalse(validate_push("codex", b))

    def test_reset_out_of_range(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":42,"weekly_reset_min":70000}
        self.assertFalse(validate_push("codex", b))

    def test_pct_is_bool_rejected(self):
        # JSON true/false must not be accepted as int (Python bool is int subclass)
        b = {"v":1,"provider":"codex","weekly_used_pct":True}
        self.assertFalse(validate_push("codex", b))

    def test_pct_zero_ok(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":0}
        self.assertTrue(validate_push("codex", b))

    def test_pct_hundred_ok(self):
        b = {"v":1,"provider":"codex","weekly_used_pct":100}
        self.assertTrue(validate_push("codex", b))

    # ---- system (was missing from the mirror — now covered) ----
    def test_system_full(self):
        b = {"v": 1, "provider": "system",
             "five_hour_used_pct": 42, "weekly_used_pct": 68, "extra_pct": 71}
        self.assertTrue(validate_push("system", b))

    def test_system_bad_extra(self):
        b = {"v": 1, "provider": "system",
             "five_hour_used_pct": 42, "extra_pct": 150}
        self.assertFalse(validate_push("system", b))

    # ---- vitals ----
    def test_vitals_full(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "weekly_used_pct": 68, "extra_pct": 71,
             "temp_c": 54, "battery_pct": 100, "uptime_min": 134}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_temp_na_is_null(self):
        # Apple Silicon: temp_c is null (not sent) — must still accept.
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "weekly_used_pct": 68}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_bad_temp(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "temp_c": 200}
        self.assertFalse(validate_push("vitals", b))

    def test_vitals_neg_temp_ok(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "temp_c": -10}
        self.assertTrue(validate_push("vitals", b))

    def test_vitals_bad_uptime(self):
        b = {"v": 1, "provider": "vitals",
             "five_hour_used_pct": 42, "uptime_min": 70000}
        self.assertFalse(validate_push("vitals", b))

    # ---- weather ----
    def test_weather_full(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "weekly_used_pct": 87,
             "weather_code": 3, "temp_min": 26, "temp_max": 34, "aqi_pm25": 25}
        self.assertTrue(validate_push("weather", b))

    def test_weather_bad_code(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "weather_code": 150}
        self.assertFalse(validate_push("weather", b))

    def test_weather_bad_pm25(self):
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "aqi_pm25": 300}
        self.assertFalse(validate_push("weather", b))

    def test_weather_bool_rejected(self):
        # bool must not sneak through as int (Python gotcha).
        b = {"v": 1, "provider": "weather",
             "five_hour_used_pct": 31, "aqi_pm25": True}
        self.assertFalse(validate_push("weather", b))

if __name__ == "__main__":
    unittest.main()
