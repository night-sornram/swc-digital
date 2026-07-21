"""Tests for the Mac vitals adapter (CPU/RAM/SSD/battery/uptime).

Mocks psutil so the tests run without a real Mac and without blocking on
cpu_percent intervals."""
import unittest
from unittest import mock
import importlib
import sys
import os
import types

# Inject a fake psutil into sys.modules before importing the adapter.
class FakeSensors:
    @staticmethod
    def battery():
        return types.SimpleNamespace(percent=80, power_plugged=True, secsleft=-2)

class FakePsutil:
    @staticmethod
    def cpu_percent(interval=0.0):
        return 42.0
    @staticmethod
    def virtual_memory():
        return types.SimpleNamespace(percent=68)
    @staticmethod
    def disk_usage(path):
        return types.SimpleNamespace(used=710, total=1000)
    @staticmethod
    def boot_time():
        return 1000.0
    @staticmethod
    def sensors_battery():
        return FakeSensors.battery()

class VitalsAdapterTests(unittest.TestCase):
    def setUp(self):
        sys.modules["psutil"] = FakePsutil
        # Force reimport.
        if "vitals_adapter" in sys.modules:
            del sys.modules["vitals_adapter"]
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
        import vitals_adapter
        self.mod = vitals_adapter

    def test_returns_expected_shape(self):
        import time
        with mock.patch("time.time", return_value=2134.0):  # uptime = 1134s = 18.9 -> 18m
            out = self.mod.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 42)
        self.assertEqual(out["weekly"]["used_pct"], 68)
        self.assertEqual(out["extra_pct"], 71)
        self.assertEqual(out["battery_pct"], 80)
        self.assertEqual(out["temp_c"], None)
        self.assertIn("uptime_min", out)

    def test_cpu_clamped(self):
        with mock.patch.object(FakePsutil, "cpu_percent", return_value=150.0):
            out = self.mod.fetch()
        self.assertEqual(out["five_hour"]["used_pct"], 100)

if __name__ == "__main__":
    unittest.main()
