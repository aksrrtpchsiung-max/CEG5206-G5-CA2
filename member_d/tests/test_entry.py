import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("entry", ROOT / "scripts/run.py")
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class EntryTests(unittest.TestCase):
    def test_main_has_24_workloads_per_variant(self):
        cases = entry.expand(entry.read_config(ROOT / "configs/main.json"))
        self.assertEqual(len(cases), 24 * 5)
        self.assertEqual(len({c["case_id"] for c in cases}), len(cases))
        self.assertEqual(cases, entry.expand(entry.read_config(ROOT / "configs/main.json")))

    def test_tuning_and_reuse_are_plans(self):
        self.assertEqual(len(entry.expand(entry.read_config(ROOT / "configs/tuning.json"))), 12)
        cases = entry.expand(entry.read_config(ROOT / "configs/reuse.json"))
        self.assertEqual({c["reuse_calls"] for c in cases}, {1, 10, 100})

    def test_impossible_skew_rejected(self):
        cfg = entry.read_config(ROOT / "configs/smoke.json")
        cfg.update(nodes=[4], mean_degrees=[2], families=["skewed"])
        with self.assertRaises(ValueError):
            entry.expand(cfg)

    def test_scaling_example(self):
        cfg = entry.read_config(ROOT / "configs/scaling.example.json")
        cases = entry.expand(cfg)
        self.assertEqual({c["threads"] for c in cases}, {1, 2, 4})
        self.assertTrue(all(c["threads"] == 1 for c in cases if c["variant"] in ("scalar", "simd")))

    def test_default_binary_cannot_produce_fake_results(self):
        binary = ROOT / "build/benchmark"
        if not binary.exists():
            self.skipTest("build default benchmark first")
        linked = entry.availability(binary)
        self.assertFalse(any(linked.values()))
        result = subprocess.run([str(binary), "scalar"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, "")
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "results"
            result = subprocess.run(["python3", str(ROOT / "scripts/run.py"), "run", "--config",
                                     str(ROOT / "configs/smoke.json"), "--out", str(out)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not linked", result.stderr)
            self.assertFalse(out.exists())

    def test_summary_requires_matching_inputs(self):
        cfg = entry.read_config(ROOT / "configs/smoke.json")
        cfg.update(variants=["scalar", "simd"], families=["uniform"])
        cases = entry.expand(cfg)
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / "cases").mkdir()
            for c in cases:
                payload = dict(calls=[], validation=[], setup_memory={}, degree={},
                               summary={"median_seconds": 2 if c["variant"] == "scalar" else 1})
                (out / "cases" / f"{c['case_id']}.json").write_text(json.dumps(payload))
            entry.export_results(out, cases)
            import csv
            with (out / "summary.csv").open() as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(float(rows[1]["scalar_speedup"]), 2)

    def test_linked_fixture_end_to_end(self):
        binary = ROOT / "build/test_fixture_runner"
        if not binary.exists():
            self.skipTest("build test_fixture_runner first")
        cfg = entry.read_config(ROOT / "configs/smoke.json")
        cfg.update(nodes=[2], mean_degrees=[1], features=[1], families=["uniform"])
        metadata = {key: "test fixture only" for key in ("cpu", "isa", "cache_sizes", "compiler", "compiler_flags", "affinity", "frequency_policy", "build_command")}
        metadata["physical_cores"] = 1
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            (temp / "config.json").write_text(json.dumps(cfg))
            (temp / "hardware.json").write_text(json.dumps(metadata))
            out = temp / "out"
            result = subprocess.run(["python3", str(ROOT / "scripts/run.py"), "run", "--config", str(temp / "config.json"),
                                     "--binary", str(binary), "--metadata", str(temp / "hardware.json"), "--out", str(out)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((out / "manifest.json").read_text())
            self.assertEqual(manifest["status"], "completed")
            self.assertEqual(manifest["result_kind"], "integration_only")
            payload = json.loads(next((out / "cases").glob("*.json")).read_text())
            self.assertEqual(len(payload["validation"]), 2)
            self.assertTrue(all(c["passed"] for c in payload["validation"]))
            self.assertEqual(len(payload["calls"]), 10)
            self.assertIn("input_digest", payload)
            for name in ("calls", "validation", "summary", "setup_memory", "degree"):
                self.assertTrue((out / f"{name}.csv").exists())


if __name__ == "__main__":
    unittest.main()
