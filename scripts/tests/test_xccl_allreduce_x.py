from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = REPO_ROOT / "scripts"
CONFIG = REPO_ROOT / "configs" / "xccl-allreduce-x" / "cann-8.5.1-ascend910b4.yaml"
sys.path.insert(0, str(SCRIPTS))

from xccl_allreduce_x import ConfigError, load_config  # noqa: E402


class ConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))

    def load(self, raw: dict) -> tuple:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.yaml"
            path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            return load_config(path)

    def test_default_catalog_has_thirty_unique_operators(self) -> None:
        scenarios, settings, baseline = load_config(CONFIG)
        self.assertEqual(30, len(scenarios))
        self.assertEqual(30, len({scenario.id for scenario in scenarios}))
        self.assertEqual(30, len({scenario.operator for scenario in scenarios}))
        self.assertEqual({"vector": 12, "cube": 6, "fused": 12}, {
            kind: sum(scenario.kind == kind for scenario in scenarios)
            for kind in ("vector", "cube", "fused")
        })
        self.assertEqual([0], baseline["submit_ranks"])
        self.assertEqual(1024, settings.memory_limit_mib)

    def test_unknown_operator_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][0]["operator"] = "dynamic_import"
        with self.assertRaisesRegex(ConfigError, "unknown operator"):
            self.load(raw)

    def test_duplicate_id_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][1]["id"] = raw["scenarios"][0]["id"]
        with self.assertRaisesRegex(ConfigError, "duplicate scenario id"):
            self.load(raw)

    def test_dtype_mismatch_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        target = next(item for item in raw["scenarios"] if item["operator"] == "quant_batch_matmul")
        target["dtype"] = "float32"
        with self.assertRaisesRegex(ConfigError, "dtype"):
            self.load(raw)

    def test_missing_shape_key_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        target = next(item for item in raw["scenarios"] if item["operator"] == "matmul")
        del target["shapes"][0]["k"]
        with self.assertRaisesRegex(ConfigError, "invalid shape keys"):
            self.load(raw)

    def test_cube_alignment_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        target = next(item for item in raw["scenarios"] if item["operator"] == "matmul")
        target["shapes"][0]["m"] = 1000
        with self.assertRaisesRegex(ConfigError, "divisible by 16"):
            self.load(raw)

    def test_vector_full_core_requires_forty_partitions(self) -> None:
        raw = deepcopy(self.raw)
        target = next(item for item in raw["scenarios"] if item["operator"] == "sigmoid")
        target["shapes"][0]["rows"] = 4
        with self.assertRaisesRegex(ConfigError, "at least 40 vector"):
            self.load(raw)

    def test_unknown_parameter_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][0]["params"] = {"callable": "os.system"}
        with self.assertRaisesRegex(ConfigError, "invalid params"):
            self.load(raw)

    def test_memory_limit_is_enforced(self) -> None:
        raw = deepcopy(self.raw)
        raw["defaults"]["memory_limit_mib"] = 1
        with self.assertRaisesRegex(ConfigError, "above limit"):
            self.load(raw)

    def test_multiple_shapes_expand_without_runner_changes(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][0]["shapes"].append({"profile": "full_core", "rows": 80, "cols": 131072})
        scenarios, _settings, _baseline = self.load(raw)
        expanded = [scenario for scenario in scenarios if scenario.id == "vector.add"]
        self.assertEqual(2, len(expanded))


class CliTests(unittest.TestCase):
    def test_dry_run_lists_catalog_without_npu_runtime(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "run-xccl-allreduce-x-matrix.py"),
                "--config",
                str(CONFIG),
                "--dry-run",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("TOTAL=30", completed.stdout)
        self.assertIn('"id": "fused.custom_top_k_top_p"', completed.stdout)


if __name__ == "__main__":
    unittest.main()
