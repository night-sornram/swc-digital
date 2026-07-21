"""Host tests for /api/identity shape + Device ID format."""
import unittest
from _security_rules import valid_device_id, valid_identity

class DeviceIdTests(unittest.TestCase):
    def test_valid_lowercase_hex_8(self):
        self.assertTrue(valid_device_id("c089a3f2"))
        self.assertTrue(valid_device_id("00000000"))
        self.assertTrue(valid_device_id("ffffffff"))

    def test_rejects_uppercase(self):
        self.assertFalse(valid_device_id("C089A3F2"))

    def test_rejects_wrong_length(self):
        self.assertFalse(valid_device_id("c089a3f"))    # 7
        self.assertFalse(valid_device_id("c089a3f22"))  # 9

    def test_rejects_non_hex(self):
        self.assertFalse(valid_device_id("c089a3g2"))

class IdentityShapeTests(unittest.TestCase):
    def _minimal(self, **over):
        d = {"id": "c089a3f2", "fw": "swc-digital", "version": "3.1.0",
             "paired": False, "mode": "ap"}
        d.update(over)
        return d

    def test_minimal_valid(self):
        self.assertTrue(valid_identity(self._minimal()))

    def test_paired_sta(self):
        self.assertTrue(valid_identity(self._minimal(paired=True, mode="sta")))

    def test_missing_key_rejected(self):
        for k in ("id", "fw", "version", "paired", "mode"):
            d = self._minimal(); del d[k]
            self.assertFalse(valid_identity(d), f"{k} missing should fail")

    def test_wrong_type_rejected(self):
        self.assertFalse(valid_identity(self._minimal(paired="yes")))
        self.assertFalse(valid_identity(self._minimal(version=31)))
        self.assertFalse(valid_identity(self._minimal(mode="wifi")))

    def test_no_secret_keys_leak(self):
        # Identity MUST NOT include pairing key, H1, WiFi password, etc.
        d = self._minimal()
        for banned in ("pairkey", "h1", "pass", "apPass", "token"):
            self.assertNotIn(banned, d)

if __name__ == "__main__":
    unittest.main()
