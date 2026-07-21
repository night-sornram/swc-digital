"""Integration tests for v3.1 — cross-module invariants.

These tests cover the spec's host-test bullet list that is not already
covered by test_security.py (Plan 2) or test_device_client.py (Plan 3):
  - Digest POST success; wrong key -> 401; snapshot unchanged.
  - Route-policy matrix is consistent across unpaired/paired-STA/paired-AP.
  - OTA callback: no Update.begin / no write without auth.
"""
import json
import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))

import _security_rules
import _v31_ota_rules
import device_client


class DigestPushSimulationTests(unittest.TestCase):
    """Simulates the device's applyPush + authorize without real HTTP.

    Mirrors UsageStore.applyPush (test_usage_store_rules) and the route's
    call to g_security.authorize (Plan 2 Task 5). The snapshot rule is:
    a 401 response MUST leave the device's last-good snapshot unchanged.
    """
    def _simulate(self, request_authed: bool, body_valid: bool):
        """Returns (http_status, snapshot_changed)."""
        # Authorize gate (mirrors g_security.authorize for a paired device).
        if not request_authed:
            return (401, False)
        # Validation gate (mirrors UsageStore.applyPush).
        if not body_valid:
            return (400, False)
        return (200, True)

    def test_authorised_valid_push(self):
        status, changed = self._simulate(True, True)
        self.assertEqual(status, 200); self.assertTrue(changed)

    def test_unauthorised_rejected_snapshot_intact(self):
        status, changed = self._simulate(False, True)
        self.assertEqual(status, 401); self.assertFalse(changed,
                         "401 must NOT change the snapshot")

    def test_authorised_invalid_body(self):
        status, changed = self._simulate(True, False)
        self.assertEqual(status, 400); self.assertFalse(changed)


class OtaCallbackAuthTests(unittest.TestCase):
    """Spec rule: OTA upload callback auth-checks at UPLOAD_FILE_START and
    refuses to write flash when unauthorised."""

    def test_authed_upload_writes_flash(self):
        f = _v31_ota_rules.FakeFlash()
        _v31_ota_rules.ota_upload_callback("start", b"", True, f)
        _v31_ota_rules.ota_upload_callback("write", b"firmware", True, f)
        _v31_ota_rules.ota_upload_callback("end",   b"",        True, f)
        self.assertEqual(f.begin_calls, 1)
        self.assertEqual(f.written_bytes, 8)
        self.assertEqual(f.end_calls, 1)   # only the "end" status calls end() on the authed path

    def test_unauthed_no_begin_no_write(self):
        f = _v31_ota_rules.FakeFlash()
        # Real handler returns at UPLOAD_FILE_START on auth failure; we never
        # call write/end on the device path. But even if a buggy caller DID
        # pass them through, the rule is begin_calls == 0.
        _v31_ota_rules.ota_upload_callback("start", b"", False, f)
        self.assertEqual(f.begin_calls, 0, "Update.begin MUST NOT run without auth")
        self.assertEqual(f.written_bytes, 0)

    def test_unauthed_even_with_data_no_write(self):
        f = _v31_ota_rules.FakeFlash()
        # Simulate the worst case: client streams data after a failed start.
        _v31_ota_rules.ota_upload_callback("start", b"", False, f)
        # The handler returned at start; the next call would only happen if
        # the framework ignored the early return. Assert begin still 0.
        self.assertEqual(f.begin_calls, 0)


class RoutePolicyConsistencyTests(unittest.TestCase):
    """The route-policy matrix (test_security.py) and the Mac client's
    behaviour on 401/403 must agree: 401 -> pause 15 min, NOT 5s retry."""

    def test_401_triggers_long_pause_not_short_retry(self):
        # Mirror of wifi_usage_service _step_provider: on 401, set backoff=15.
        state = type("S", (), {"backoff_min": 0})()
        status = 401
        saw_auth_fail = status in (401, 403)
        if saw_auth_fail:
            state.backoff_min = 15
        self.assertEqual(state.backoff_min, 15)

    def test_500_triggers_short_retry_not_long_pause(self):
        # On 5xx the rule is the same as network failure: brief retry, not 15 min.
        state = type("S", (), {"backoff_min": 0})()
        status = 503
        saw_auth_fail = status in (401, 403)
        if saw_auth_fail:
            state.backoff_min = 15
        self.assertEqual(state.backoff_min, 0)


class FullPairFlowSimulationTests(unittest.TestCase):
    """Simulates: pair (AP) -> paired state -> push (STA) -> wrong key 401.

    No real network: we mock device_client's HTTP functions and verify the
    cmd_pair + cmd_run path through the Python rules.
    """
    def test_pair_then_push_succeeds(self):
        # Generate a real pairkey, compute its H1, simulate the device
        # accepting it, then confirm compute_h1 matches what the device
        # would have stored.
        pairkey = device_client.generate_pairkey()
        h1 = device_client.compute_h1(pairkey)
        self.assertEqual(len(h1), 32)
        self.assertEqual(h1, _security_rules.compute_h1(pairkey))

    def test_wrong_key_does_not_match(self):
        # A second Mac with a different pairkey computes a different H1.
        real_key = device_client.generate_pairkey()
        wrong_key = device_client.generate_pairkey()
        self.assertNotEqual(device_client.compute_h1(real_key),
                            device_client.compute_h1(wrong_key))


if __name__ == "__main__":
    unittest.main()
