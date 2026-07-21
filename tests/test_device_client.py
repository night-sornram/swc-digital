"""Host tests for device_client + mDNS Device-ID filter (Plan 3)."""
import importlib.util
import json
import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))

import device_client
import aiusage_mdns


class PairkeyTests(unittest.TestCase):
    def test_crockford_length(self):
        k = device_client.generate_pairkey()
        self.assertEqual(len(k), 16)
        for c in k:
            self.assertIn(c, device_client.CROCKFORD)

    def test_no_ambiguous_chars(self):
        for _ in range(50):
            k = device_client.generate_pairkey()
            self.assertNotIn("I", k); self.assertNotIn("L", k)
            self.assertNotIn("O", k); self.assertNotIn("U", k)

    def test_different_each_call(self):
        keys = {device_client.generate_pairkey() for _ in range(20)}
        self.assertGreater(len(keys), 1)


class H1Tests(unittest.TestCase):
    def test_md5_hex_32(self):
        h = device_client.compute_h1("ABCDEFGHJKLMNPQR")
        self.assertEqual(len(h), 32)

    def test_matches_security_rules(self):
        import _security_rules
        self.assertEqual(device_client.compute_h1("test12345678"),
                         _security_rules.compute_h1("test12345678"))


class KeychainMirrorTests(unittest.TestCase):
    """Keychain is mocked — we verify the subprocess calls, not real storage."""

    def test_set_calls_security_cli(self):
        with mock.patch("subprocess.run") as run:
            # Default MagicMock.returncode is truthy; force a success return so
            # keychain_set's error branch does not fire during the call sequence.
            run.return_value = mock.MagicMock(returncode=0, stderr="")
            device_client.keychain_set("c089a3f2", "PAIRKEY1234567")
        # delete-then-add pattern: 2 calls minimum.
        self.assertGreaterEqual(run.call_count, 2)
        add_call = run.call_args_list[-1]
        self.assertIn("add-generic-password", " ".join(add_call.args[0]))
        self.assertIn("com.night.swc-digital.device-c089a3f2", " ".join(add_call.args[0]))

    def test_get_returns_password(self):
        with mock.patch("subprocess.run") as run:
            run.return_value = mock.MagicMock(stdout="PAIRKEY1234567\n", returncode=0)
            k = device_client.keychain_get("c089a3f2")
        self.assertEqual(k, "PAIRKEY1234567")


class DeviceIdFilterTests(unittest.TestCase):
    def test_filter_by_id(self):
        discovered = [("http://1.2.3.4/api/usage", "c089a3f2"),
                      ("http://5.6.7.8/api/usage", "deadbeef")]
        with mock.patch.object(aiusage_mdns, "discover", return_value=discovered):
            got = aiusage_mdns.all_targets([], device_id="c089a3f2")
        self.assertEqual(got, [("http://1.2.3.4/api/usage", "c089a3f2")])

    def test_fail_closed_on_duplicate_id(self):
        discovered = [("http://1.2.3.4/api/usage", "c089a3f2"),
                      ("http://5.6.7.8/api/usage", "c089a3f2")]
        with mock.patch.object(aiusage_mdns, "discover", return_value=discovered):
            got = aiusage_mdns.all_targets([], device_id="c089a3f2")
        self.assertEqual(got, [], "duplicate id should fail closed")

    def test_explicit_then_mdns_dedup(self):
        discovered = [("http://5.6.7.8/api/usage", "c089a3f2")]
        with mock.patch.object(aiusage_mdns, "discover", return_value=discovered):
            got = aiusage_mdns.all_targets(["http://1.2.3.4/api/usage"], device_id=None)
        self.assertEqual(got, [("http://1.2.3.4/api/usage", ""),
                                ("http://5.6.7.8/api/usage", "c089a3f2")])


if __name__ == "__main__":
    unittest.main()
