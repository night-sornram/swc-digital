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

if __name__ == "__main__":
    unittest.main()
