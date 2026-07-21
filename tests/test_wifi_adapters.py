"""Host tests for the Wi-Fi adapters and service logic (spec §host tests)."""
import importlib.util
import json
import os
import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))

import codex_wifi_adapter
import zai_wifi_adapter
import _wifi_adapter_fixtures as F


def _load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class CodexAdapterTests(unittest.TestCase):
    def _patch_fetch(self, payload):
        # NOTE: patch.object requires the immediate parent object + the leaf
        # attribute name. A dotted string is treated as a single literal attr
        # name, so we traverse to codex_wifi_adapter.urllib.request first.
        return mock.patch.object(codex_wifi_adapter.urllib.request, "urlopen",
                                 _make_fake_urlopen(payload))

    def test_weekly_only_five_hour_null(self):
        with self._patch_fetch(F.CODEX_WEEKLY_ONLY):
            out = codex_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertEqual(out["weekly"]["used_pct"], 42)

    def test_both_windows_either_slot(self):
        with self._patch_fetch(F.CODEX_BOTH):
            out = codex_wifi_adapter.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 5)
        self.assertEqual(out["weekly"]["used_pct"], 91)

    def test_unknown_duration_ignored(self):
        with self._patch_fetch(F.CODEX_UNKNOWN_DURATION):
            out = codex_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertIsNone(out["weekly"])

    def test_reset_rounded_up(self):
        with self._patch_fetch(F.CODEX_WEEKLY_ONLY):   # 3600s -> 60 min exactly
            out = codex_wifi_adapter.fetch()
        self.assertEqual(out["weekly"]["reset_min"], 60)


class ZaiAdapterTests(unittest.TestCase):
    def _patch_fetch(self, payload):
        # NOTE: see CodexAdapterTests — traverse to urllib.request first.
        return mock.patch.object(zai_wifi_adapter.urllib.request, "urlopen",
                                 _make_fake_urlopen(payload))

    def setUp(self):
        # Settings must look valid (host-allowed) or fetch bails before the request.
        self._cfg = mock.patch.dict(os.environ, {})
        # Patch settings read to return an allowed host + dummy token.
        self._settings = mock.patch.object(zai_wifi_adapter, "_read_settings",
                                           return_value=("https://api.z.ai", "dummy"))
        self._settings.start()
        self.addCleanup(self._settings.stop)

    def test_maps_both_windows(self):
        with self._patch_fetch(F.ZAI_BOTH):
            out = zai_wifi_adapter.fetch()
        # NOTE: Python's round() uses banker's rounding (round-half-to-even),
        # so int(round(12.5)) == 12, not 13. The plan comment was wrong about
        # Python's rounding mode; the adapter's int(round(pct)) is unchanged.
        self.assertEqual(out["five_hour"]["used_pct"], 12)
        self.assertEqual(out["weekly"]["used_pct"], 88)

    def test_missing_quota_all_null(self):
        with self._patch_fetch(F.ZAI_EMPTY):
            out = zai_wifi_adapter.fetch()
        self.assertIsNone(out["five_hour"])
        self.assertIsNone(out["weekly"])

    def test_edge_percent_and_expired_reset(self):
        with self._patch_fetch(F.ZAI_EDGE):
            out = zai_wifi_adapter.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 0)
        self.assertEqual(out["five_hour"]["reset_min"], 0)   # expired clamped
        self.assertEqual(out["weekly"]["used_pct"], 100)


class ErrorHandlingTests(unittest.TestCase):
    def test_codex_401_raises(self):
        import urllib.error
        def _raise(*a, **kw):
            raise urllib.error.HTTPError(a[0].full_url if hasattr(a[0], "full_url") else "u",
                                         401, "Unauthorized", {}, None)
        with mock.patch.object(codex_wifi_adapter.urllib.request, "urlopen", _raise), \
             mock.patch.object(codex_wifi_adapter, "_read_token", return_value="x"):
            with self.assertRaises(codex_wifi_adapter.CodexAdapterError):
                codex_wifi_adapter.fetch()

    def test_zai_timeout_raises(self):
        with mock.patch.object(zai_wifi_adapter, "_read_settings",
                               return_value=("https://api.z.ai", "x")), \
             mock.patch.object(zai_wifi_adapter.urllib.request, "urlopen",
                               side_effect=TimeoutError):
            with self.assertRaises(zai_wifi_adapter.ZaiAdapterError):
                zai_wifi_adapter.fetch()


class LogSafetyTests(unittest.TestCase):
    """The service must NEVER log token/header/account/body. We assert the
    adapter modules do not print those fields in their smoke-mode output."""
    def test_codex_smoke_no_secret(self):
        # We cannot easily capture stdout from a subprocess here; instead assert
        # that the module source does not contain a print() of the token/header.
        src = (TOOLS / "codex_wifi_adapter.py").read_text()
        self.assertNotIn("print(token", src)
        self.assertNotIn("print(headers", src)

    def test_service_does_not_log_body(self):
        src = (TOOLS / "wifi_usage_service.py").read_text()
        # log.* calls must not pass the body or response.read() output.
        # Heuristic: no line contains 'log' and 'body' together.
        for line in src.splitlines():
            if "log." in line and "body" in line and "make_body" not in line and "_make_body" not in line:
                self.fail(f"service line may log body: {line}")


def _make_fake_urlopen(payload):
    """Return a fake urlopen context manager that yields `payload` as JSON."""
    import io
    class _Resp:
        status = 200
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self): return json.dumps(payload).encode("utf-8")
    def _fake(req, timeout=None):
        return _Resp()
    return _fake


class DiscoveryFallbackTests(unittest.TestCase):
    def test_explicit_only_when_no_zeroconf(self):
        # Force discover() to return [] (zeroconf not installed in test env).
        import aiusage_mdns
        with mock.patch.object(aiusage_mdns, "discover", return_value=[]):
            urls = aiusage_mdns.all_targets(["http://1.2.3.4/api/usage"], mdns_timeout=0)
        self.assertEqual(urls, ["http://1.2.3.4/api/usage"])

    def test_dedup_explicit_then_mdns(self):
        import aiusage_mdns
        with mock.patch.object(aiusage_mdns, "discover",
                               return_value=["http://5.6.7.8/api/usage",
                                             "http://1.2.3.4/api/usage"]):   # dup of explicit
            urls = aiusage_mdns.all_targets(["http://1.2.3.4/api/usage"], mdns_timeout=0)
        self.assertEqual(urls, ["http://1.2.3.4/api/usage", "http://5.6.7.8/api/usage"])


if __name__ == "__main__":
    unittest.main()
