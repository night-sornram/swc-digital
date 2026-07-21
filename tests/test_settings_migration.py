"""Host tests for the Settings v3 migration (spec §host tests).
Mirrors src/Settings.cpp loadSettings() migration rules."""
import unittest

def migrate_v2_to_v3(old: dict) -> dict:
    """Apply the same lift/normalize that loadSettings does for fileVer<3."""
    out = {"schemaVersion": 3}
    for k in ("hostname", "apSsid", "apPass", "httpTimeout",
              "brightness", "autoBrightness", "backlightInverted", "rotation"):
        if k in old:
            out[k] = old[k]
    # WiFi list (authoritative when present).
    if "wifi" in old:
        out["wifi"] = old["wifi"]
    elif "staSsid" in old:
        # legacy single-network mirror
        out["wifi"] = [{"ssid": old["staSsid"], "pass": old.get("staPass", "")}]
    # Clock slice (kept verbatim).
    if "clock" in old:
        out["clock"] = old["clock"]
    # Mode: any old token -> AUTO.
    out["usage"] = {"mode": "auto"}
    # carouselSec -> autoRotateSec, clamped to 5..3600.
    cs = old.get("carouselSec", 30)
    out["usage"]["autoRotateSec"] = max(5, min(3600, int(cs)))
    return out

class MigrationTests(unittest.TestCase):
    def test_lifts_wifi_display_clock(self):
        old = {
            "hostname": "smalltv-ab12", "staSsid": "home", "staPass": "secret",
            "brightness": 70, "rotation": 2, "httpTimeout": 8000,
            "backlightInverted": True, "autoBrightness": False,
            "mode": "stocks", "carouselSec": 45,
            "ticker": {"webhookUrl": "x"}, "usage": {"usageUrl": "y"},
            "clock": {"tz": "Asia/Bangkok"},
        }
        new = migrate_v2_to_v3(old)
        self.assertEqual(new["schemaVersion"], 3)
        self.assertEqual(new["hostname"], "smalltv-ab12")
        self.assertEqual(new["wifi"], [{"ssid": "home", "pass": "secret"}])
        self.assertEqual(new["brightness"], 70)
        self.assertEqual(new["rotation"], 2)
        self.assertEqual(new["clock"], {"tz": "Asia/Bangkok"})
        self.assertEqual(new["usage"], {"mode": "auto", "autoRotateSec": 45})

    def test_drops_deleted_keys(self):
        old = {"mode": "radar", "carouselSec": 30,
               "ticker": {"symbols": []}, "radar": {"lat": 0},
               "usage": {"usageUrl": "x"}, "carouselTicker": True}
        new = migrate_v2_to_v3(old)
        for banned in ("ticker", "radar", "usageUrl", "carouselTicker",
                       "staSsid", "staPass", "mode"):
            self.assertNotIn(banned, new, f"{banned} leaked into v3 schema")
        # The old top-level "usage" object is gone; only the new usage slice remains.
        self.assertEqual(set(new["usage"].keys()), {"mode", "autoRotateSec"})

    def test_any_old_mode_maps_to_auto(self):
        for tok in ("stocks", "usage", "radar", "carousel"):
            old = {"mode": tok, "carouselSec": 30}
            new = migrate_v2_to_v3(old)
            self.assertEqual(new["usage"]["mode"], "auto")

    def test_clamps_autorotate(self):
        self.assertEqual(migrate_v2_to_v3({"carouselSec": 1})["usage"]["autoRotateSec"], 5)
        self.assertEqual(migrate_v2_to_v3({"carouselSec": 99999})["usage"]["autoRotateSec"], 3600)

if __name__ == "__main__":
    unittest.main()
