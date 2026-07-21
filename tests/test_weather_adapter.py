"""Tests for the weather adapter (open-meteo + open-meteo AQI).

Mocks urllib so the tests run offline."""
import json
import unittest
from unittest import mock
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))


def _fake_urlopen(url, timeout):
    # Return fixture based on which API the URL hits.
    if "air-quality-api" in url:
        body = {
            "current": {"pm2_5": 25.2, "european_aqi": 36}
        }
    else:
        body = {
            "current": {"temperature_2m": 28.7, "weather_code": 3},
            "daily": {"temperature_2m_max": [33.4], "temperature_2m_min": [25.9]},
        }
    resp = mock.MagicMock()
    resp.read.return_value = json.dumps(body).encode()
    resp.__enter__ = mock.MagicMock(return_value=resp)
    resp.__exit__ = mock.MagicMock(return_value=False)
    return resp


class WeatherAdapterTests(unittest.TestCase):
    def setUp(self):
        import weather_adapter
        self.mod = weather_adapter

    def test_fetch_returns_expected_shape(self):
        with mock.patch("urllib.request.urlopen", side_effect=_fake_urlopen):
            out = self.mod.fetch(13.7563, 100.5018)
        self.assertEqual(out["five_hour"]["used_pct"], 29)   # 28.7 -> 29
        self.assertEqual(out["weekly"]["used_pct"], 36)      # european_aqi
        self.assertEqual(out["weather_code"], 3)
        self.assertEqual(out["temp_min"], 26)                # 25.9 -> 26
        self.assertEqual(out["temp_max"], 33)                # 33.4 -> 33
        self.assertEqual(out["aqi_pm25"], 25)                # 25.2 -> 25

    def test_fetch_raises_on_http_error(self):
        with mock.patch("urllib.request.urlopen", side_effect=Exception("boom")):
            with self.assertRaises(self.mod.WeatherError):
                self.mod.fetch(13.7563, 100.5018)

    def test_fetch_raises_on_missing_current(self):
        body = {"current": {}}  # empty
        resp = mock.MagicMock()
        resp.read.return_value = json.dumps(body).encode()
        resp.__enter__ = mock.MagicMock(return_value=resp)
        resp.__exit__ = mock.MagicMock(return_value=False)
        with mock.patch("urllib.request.urlopen", return_value=resp):
            with self.assertRaises(self.mod.WeatherError):
                self.mod.fetch(13.7563, 100.5018)


if __name__ == "__main__":
    unittest.main()
