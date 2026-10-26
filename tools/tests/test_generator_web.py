import importlib.util
import json
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace


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
            result = manager.finalize({"meter_value": 5.16, "meter_max_value": 6.02})
            self.assertTrue(Path(result["trial_dir"], "report.html").exists())
            self.assertTrue(Path(result["trial_dir"], "trial_summary.csv").exists())
            self.assertTrue(Path(result["trial_dir"], "imu_raw.csv").exists())
            saved_summary = json.loads(Path(result["trial_dir"], "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(saved_summary["metrics"]["meter_max_value"], 6.02)
            self.assertIn("meter_max_value", Path(result["trial_dir"], "trial_summary.csv").read_text(encoding="utf-8"))
            self.assertIn("20秒最大显示值", Path(result["trial_dir"], "report.html").read_text(encoding="utf-8"))
            self.assertTrue(result["trial_summary_url"].endswith("/trial_summary.csv"))
            self.assertTrue(result["raw_csv_url"].endswith("/imu_raw.csv"))
            self.assertEqual(manager.snapshot()["state"], "complete")
            records = generator_web.history_records(root)
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["generator"], "GEN-WEB")
            self.assertEqual(records[0]["meter_value"], 5.16)
            self.assertEqual(records[0]["meter_max_value"], 6.02)
            self.assertIsNotNone(records[0]["accel_rms"])
            self.assertIsNotNone(records[0]["velocity_index"])
            self.assertIsNotNone(records[0]["normalized"])
            self.assertTrue(records[0]["trial_summary_url"].endswith("/trial_summary.csv"))

    def test_discard_unsaved_capture_resets_without_history(self):
        with tempfile.TemporaryDirectory() as root:
            manager = generator_web.CaptureManager(generator_test, root)
            manager.start(demo_payload(), demo=True)
            deadline = time.monotonic() + 3
            while manager.snapshot()["state"] == "capturing" and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertEqual(manager.snapshot()["state"], "awaiting_meter")
            status = manager.discard()
            self.assertEqual(status["state"], "idle")
            self.assertEqual(status["sample_count"], 0)
            self.assertEqual(generator_web.history_records(root), [])

    def test_saved_trial_moves_to_recoverable_trash(self):
        with tempfile.TemporaryDirectory() as root:
            manager = generator_web.CaptureManager(generator_test, root)
            manager.start(demo_payload(), demo=True)
            deadline = time.monotonic() + 3
            while manager.snapshot()["state"] == "capturing" and time.monotonic() < deadline:
                time.sleep(0.02)
            result = manager.finalize({"meter_value": 5.16, "meter_max_value": 6.02})
            deleted = generator_web.trash_trial(root, result["trial"])
            self.assertEqual(deleted["deleted"], result["trial"])
            self.assertEqual(generator_web.history_records(root), [])
            recovery = Path(deleted["recoverable_at"])
            self.assertTrue(recovery.is_dir())
            self.assertTrue((recovery / "summary.json").is_file())

    def test_trash_rejects_path_outside_output_root(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "路径无效"):
                generator_web.trash_trial(root, "../outside")

    def test_rejects_maximum_below_average(self):
        with tempfile.TemporaryDirectory() as root:
            manager = generator_web.CaptureManager(generator_test, root)
            manager.start(demo_payload(), demo=True)
            deadline = time.monotonic() + 3
            while manager.snapshot()["state"] == "capturing" and time.monotonic() < deadline:
                time.sleep(0.02)
            with self.assertRaisesRegex(ValueError, "不能小于平均显示值"):
                manager.finalize({"meter_value": 5.16, "meter_max_value": 4.8})

    def test_history_reads_legacy_summary_without_maximum(self):
        with tempfile.TemporaryDirectory() as root:
            trial = Path(root, "GEN-OLD", "T0", "legacy_open_r01")
            trial.mkdir(parents=True)
            summary = {
                "manifest": {
                    "created_at": "2026-10-01T10:00:00+08:00",
                    "generator": "GEN-OLD",
                    "revision": "T0",
                    "mode": "open",
                    "repeat": 1,
                    "waveform": "ac",
                },
                "metrics": {
                    "meter_value": 5.16,
                    "meter_unit": "V",
                    "valid": True,
                },
            }
            (trial / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
            records = generator_web.history_records(root)
            self.assertEqual(records[0]["meter_value"], 5.16)
            self.assertIsNone(records[0]["meter_max_value"])


class SerialPortDetectionTests(unittest.TestCase):
    def test_marks_nrf52_adapter_as_detected_rf_host(self):
        port = SimpleNamespace(
            device="/dev/ttyACM0",
            vid=0x1915,
            pid=0x521A,
            description="nRF52 USB CDC BLE Demo",
        )
        core = SimpleNamespace(
            list_serial_ports=lambda: [port],
            rf_adapter_name=generator_test.rf_adapter_name,
        )
        result = generator_web.serial_ports(core)
        self.assertTrue(result["rf_detected"])
        self.assertTrue(result["ports"][0]["recommended"])
        self.assertEqual(result["ports"][0]["rf_adapter"], "WT9011DCL-RF nRF52 RF 主机")

    def test_does_not_label_unknown_serial_port_as_rf_host(self):
        port = SimpleNamespace(
            device="/dev/ttyACM1",
            vid=0x1234,
            pid=0x5678,
            description="Other serial device",
        )
        core = SimpleNamespace(
            list_serial_ports=lambda: [port],
            rf_adapter_name=generator_test.rf_adapter_name,
        )
        result = generator_web.serial_ports(core)
        self.assertFalse(result["rf_detected"])
        self.assertFalse(result["ports"][0]["recommended"])
        self.assertIsNone(result["ports"][0]["rf_adapter"])


if __name__ == "__main__":
    unittest.main()
