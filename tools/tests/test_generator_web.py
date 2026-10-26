import importlib.util
import tempfile
import time
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS_DIR / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generator_test = load_module("generator_test_web_core", "generator_test.py")
generator_web = load_module("generator_web", "generator_web.py")


def demo_payload():
    return {
        "generator": "GEN-WEB",
        "revision": "T0",
        "mode": "open",
        "repeat": 1,
        "waveform": "dc",
        "meter_model": "test-meter",
        "meter_range": "DC 20 V",
        "operator": "test",
        "pre_seconds": 0,
        "active_seconds": 0.2,
        "post_seconds": 0,
        "target_frequency": 2.5,
        "sample_rate": 30,
    }


class WebValidationTests(unittest.TestCase):
    def test_requires_waveform_confirmation(self):
        payload = demo_payload()
        payload["waveform"] = ""
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "AC 或 DC"):
                generator_web.build_capture_args(payload, root, demo=True)

    def test_demo_capture_finalize_and_history(self):
        with tempfile.TemporaryDirectory() as root:
            manager = generator_web.CaptureManager(generator_test, root)
            manager.start(demo_payload(), demo=True)
            deadline = time.monotonic() + 3
            while manager.snapshot()["state"] == "capturing" and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertEqual(manager.snapshot()["state"], "awaiting_meter")
            result = manager.finalize({"meter_value": 5.16})
            self.assertTrue(Path(result["trial_dir"], "report.html").exists())
            self.assertEqual(manager.snapshot()["state"], "complete")
            records = generator_web.history_records(root)
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["generator"], "GEN-WEB")
            self.assertEqual(records[0]["meter_value"], 5.16)


if __name__ == "__main__":
    unittest.main()
