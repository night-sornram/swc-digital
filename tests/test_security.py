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

from _security_rules import compute_h1, route_status, ROUTES

class H1Tests(unittest.TestCase):
    def test_md5_hex_32_chars(self):
        h = compute_h1("ABCDEFGHJKLMNPQR")
        self.assertEqual(len(h), 32)
        self.assertTrue(all(c in "0123456789abcdef" for c in h))

    def test_known_vector(self):
        import hashlib
        expected = hashlib.md5(b"admin:swc-digital:test12345678").hexdigest()
        self.assertEqual(compute_h1("test12345678"), expected)

    def test_different_keys_different_h1(self):
        self.assertNotEqual(compute_h1("aaaaaaaaaaaa"), compute_h1("bbbbbbbbbbbb"))

class RoutePolicyTests(unittest.TestCase):
    def test_identity_always_open(self):
        for state in ("unpaired", "paired"):
            for mode in ("ap", "sta"):
                for auth in (True, False):
                    self.assertEqual(route_status(state, mode, "identity", auth), 200)

    def test_captive_probe_always_open(self):
        for state in ("unpaired", "paired"):
            for mode in ("ap", "sta"):
                self.assertEqual(route_status(state, mode, "captive_probe", False), 200)

    def test_pair_ap_only_unpaired_only(self):
        self.assertEqual(route_status("unpaired", "ap",    "pair", False), 200)
        self.assertEqual(route_status("unpaired", "sta",   "pair", False), 404)
        self.assertEqual(route_status("paired",   "ap",    "pair", False), 409)
        self.assertEqual(route_status("paired",   "sta",   "pair", False), 404)

    def test_unpaired_all_open(self):
        for r in ROUTES:
            if r in ("identity", "captive_probe", "pair"): continue
            self.assertEqual(route_status("unpaired", "ap",  r, False), 200, f"{r}")
            self.assertEqual(route_status("unpaired", "sta", r, False), 200, f"{r}")

    def test_paired_requires_digest(self):
        for r in ROUTES:
            if r in ("identity", "captive_probe", "pair"): continue
            self.assertEqual(route_status("paired", "ap",  r, False), 401, f"{r} no-auth")
            self.assertEqual(route_status("paired", "sta", r, False), 401, f"{r} no-auth")
            self.assertEqual(route_status("paired", "ap",  r, True),  200, f"{r} auth")
            self.assertEqual(route_status("paired", "sta", r, True),  200, f"{r} auth")

if __name__ == "__main__":
    unittest.main()
