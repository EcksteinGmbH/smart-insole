import importlib.util
import json
import struct
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "generator_test.py"
SPEC = importlib.util.spec_from_file_location("generator_test", MODULE_PATH)
generator_test = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator_test)


def simulated_args(root, revision="T0", meter_value=5.0, repeat=1):
    return Namespace(
        generator="GEN-A",
        revision=revision,
        mode="open",
        repeat=repeat,
        meter_value=meter_value,
        meter_unit=None,
        meter_model="test-meter",
        meter_range="auto",
        waveform="unknown",
        test_point="output_pogo_pins",
        operator="test",
        notes="",
        pre_seconds=3.0,
        active_seconds=12.0,
        post_seconds=3.0,
        target_frequency=2.5,
        sensor_bandwidth=20.0,
        device_id=7,
        output_root=str(root),
        trial_dir=None,
        sample_rate=30.0,
        frequency=2.5,
        amplitude_g=3.0,
        noise_g=0.01,
        seed=repeat,
    )


class ParserTests(unittest.TestCase):
    def test_parser_resynchronizes_and_scales_signed_values(self):
        values = (-16384, 8192, 2048, -16384, 8192, 0, 1, -2, 3, -16384, 8192, 0, 395)
        packet = bytes((7, 0x55, 0x61)) + struct.pack("<13h", *values)
        parser = generator_test.WitRfParser()
        self.assertEqual(parser.feed(b"noise" + packet[:8]), [])
        frames = parser.feed(packet[8:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["device_id"], 7)
        self.assertAlmostEqual(frames[0]["ax_g"], -8.0)
        self.assertAlmostEqual(frames[0]["ay_g"], 4.0)
        self.assertAlmostEqual(frames[0]["gx_dps"], -1000.0)
        self.assertAlmostEqual(frames[0]["roll_deg"], -90.0)
        self.assertAlmostEqual(frames[0]["battery_v"], 3.95)
        self.assertEqual(parser.discarded_bytes, 5)


class AnalysisTests(unittest.TestCase):
    def test_simulated_motion_finds_frequency_and_primary_axis(self):
        with tempfile.TemporaryDirectory() as root:
            args = simulated_args(root)
            rows = generator_test.simulate_rows(args)
            manifest = generator_test.manifest_from_args(args, "simulation")
            metrics = generator_test.calculate_metrics(rows, manifest)
        self.assertTrue(metrics["valid"], metrics["invalid_reasons"])
        self.assertEqual(metrics["primary_axis"], "x")
        self.assertAlmostEqual(metrics["dominant_frequency_hz"], 2.5, delta=0.05)
        self.assertGreater(metrics["velocity_index_mps"], 0)
        self.assertGreater(metrics["normalized_meter_per_mps"], 0)

    def test_trial_outputs_and_comparison(self):
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            t0_root = root_path / "t0"
            t1_root = root_path / "t1"
            for repeat, value in enumerate((5.0, 5.1, 4.9), 1):
                args = simulated_args(t0_root, "T0", value, repeat)
                rows = generator_test.simulate_rows(args)
                manifest = generator_test.manifest_from_args(args, "simulation")
                generator_test.save_and_analyze(args, manifest, rows)
            for repeat, value in enumerate((4.9, 5.0, 4.8), 1):
                args = simulated_args(t1_root, "T1", value, repeat)
                rows = generator_test.simulate_rows(args)
                manifest = generator_test.manifest_from_args(args, "simulation")
                generator_test.save_and_analyze(args, manifest, rows)
            output = root_path / "comparison"
            result = generator_test.compare_roots(
                Namespace(
                    baseline=str(t0_root),
                    candidate=str(t1_root),
                    output_dir=str(output),
                    minimum_retention=90.0,
                    maximum_cv=15.0,
                    minimum_trials=3,
                )
            )
            self.assertEqual(len(result["rows"]), 1)
            self.assertEqual(result["rows"][0]["verdict"], "通过")
            self.assertTrue((output / "comparison.html").exists())
            parsed = json.loads((output / "comparison.json").read_text(encoding="utf-8"))
            self.assertEqual(parsed["baseline_valid_trials"], 3)


if __name__ == "__main__":
    unittest.main()
