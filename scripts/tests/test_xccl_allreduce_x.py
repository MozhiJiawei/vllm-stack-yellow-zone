from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = REPO_ROOT / "scripts"
CONFIG = REPO_ROOT / "configs" / "xccl-allreduce-x" / "cann-8.5.1-ascend910b4.yaml"
TOPK_TOPP_REGRESSION = (
    REPO_ROOT
    / "configs"
    / "xccl-allreduce-x"
    / "regressions"
    / "custom-top-k-top-p-fp32-b4-v151936.yaml"
)
FFN_REGRESSION = (
    REPO_ROOT
    / "configs"
    / "xccl-allreduce-x"
    / "regressions"
    / "ffn-bf16-t2048-h5120-i6400-gelu.yaml"
)
ROUND2_EXPLORATION = (
    REPO_ROOT
    / "configs"
    / "xccl-allreduce-x"
    / "exploration"
    / "round2-good-operators-50.yaml"
)
sys.path.insert(0, str(SCRIPTS))

from xccl_allreduce_x import (  # noqa: E402
    ConfigError,
    build_operation,
    load_config,
    should_abort,
)


class ConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))

    def load(self, raw: dict) -> tuple:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.yaml"
            path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            return load_config(path)

    def test_default_catalog_has_thirty_unique_operator_classes(self) -> None:
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
        self.assertTrue(
            all(scenario.shape["profile"] == "other" for scenario in scenarios)
        )

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

    def test_other_profile_allows_low_parallel_shapes(self) -> None:
        raw = deepcopy(self.raw)
        target = next(item for item in raw["scenarios"] if item["operator"] == "sigmoid")
        target["shapes"][0]["rows"] = 4
        scenarios, _settings, _baseline = self.load(raw)
        scenario = next(item for item in scenarios if item.operator == "sigmoid")
        self.assertEqual(4, scenario.shape["rows"])

    def test_regression_preserves_original_blocking_topk_topp_input(self) -> None:
        scenarios, settings, _baseline = load_config(TOPK_TOPP_REGRESSION)
        self.assertEqual(1, len(scenarios))
        scenario = scenarios[0]
        self.assertEqual("custom_top_k_top_p", scenario.operator)
        self.assertEqual("float32", scenario.dtype)
        self.assertEqual(
            {"profile": "regression", "batch": 4, "vocab": 151936},
            scenario.shape,
        )
        self.assertEqual({"top_k": 50, "top_p": 0.9}, scenario.params)
        self.assertEqual(
            {"preflight": "PASS", "contention": "BLOCKED_BY_A_ALLREDUCE"},
            scenario.expect,
        )
        self.assertEqual(3, settings.repeat)

    def test_invalid_expected_result_is_rejected(self) -> None:
        raw = yaml.safe_load(TOPK_TOPP_REGRESSION.read_text(encoding="utf-8"))
        raw["scenarios"][0]["expect"]["contention"] = "PASS_OR_BLOCKED"
        with self.assertRaisesRegex(ConfigError, "invalid contention expectation"):
            self.load(raw)

    def test_ffn_regression_is_independent_and_exact(self) -> None:
        scenarios, settings, _baseline = load_config(FFN_REGRESSION)
        self.assertEqual(1, len(scenarios))
        scenario = scenarios[0]
        self.assertEqual("regression.ffn.bf16.t2048.h5120.i6400.gelu", scenario.id)
        self.assertEqual("ffn", scenario.operator)
        self.assertEqual("bfloat16", scenario.dtype)
        self.assertEqual(
            {
                "profile": "regression",
                "tokens": 2048,
                "hidden": 5120,
                "intermediate": 6400,
            },
            scenario.shape,
        )
        self.assertEqual({"activation": "gelu"}, scenario.params)
        self.assertEqual(
            {"preflight": "PASS", "contention": "BLOCKED_BY_A_ALLREDUCE"},
            scenario.expect,
        )
        self.assertEqual(3, settings.repeat)

    def test_unknown_shape_profile_is_rejected(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][0]["shapes"][0]["profile"] = "arbitrary"
        with self.assertRaisesRegex(ConfigError, "profile must be"):
            self.load(raw)

    def test_legacy_shape_profiles_are_rejected(self) -> None:
        for profile in ("full_core", "issue_anchor"):
            with self.subTest(profile=profile):
                raw = deepcopy(self.raw)
                raw["scenarios"][0]["shapes"][0]["profile"] = profile
                with self.assertRaisesRegex(ConfigError, "profile must be"):
                    self.load(raw)

    def test_regression_profile_requires_exact_expectations(self) -> None:
        raw = yaml.safe_load(TOPK_TOPP_REGRESSION.read_text(encoding="utf-8"))
        del raw["scenarios"][0]["expect"]
        with self.assertRaisesRegex(ConfigError, "regression profile requires"):
            self.load(raw)

    def test_other_profile_rejects_expectations(self) -> None:
        raw = deepcopy(self.raw)
        raw["scenarios"][0]["expect"] = {
            "preflight": "PASS",
            "contention": "BLOCKED_BY_A_ALLREDUCE",
        }
        with self.assertRaisesRegex(ConfigError, "must not declare expectations"):
            self.load(raw)

    def test_round2_has_fifty_other_cases_and_excludes_known_bad_families(self) -> None:
        scenarios, settings, _baseline = load_config(ROUND2_EXPLORATION)
        self.assertEqual(50, len(scenarios))
        self.assertEqual(50, len({scenario.id for scenario in scenarios}))
        self.assertEqual(
            {"vector": 22, "cube": 11, "fused": 17},
            {
                kind: sum(scenario.kind == kind for scenario in scenarios)
                for kind in ("vector", "cube", "fused")
            },
        )
        self.assertFalse(
            {"ffn", "topk", "npu_top_k_top_p", "custom_top_k_top_p"}
            & {scenario.operator for scenario in scenarios}
        )
        self.assertTrue(
            all(scenario.shape["profile"] == "other" for scenario in scenarios)
        )
        self.assertTrue(all(not scenario.expect for scenario in scenarios))
        self.assertEqual(1, settings.repeat)

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
        raw["scenarios"][0]["shapes"].append({"profile": "other", "rows": 80, "cols": 131072})
        scenarios, _settings, _baseline = self.load(raw)
        expanded = [scenario for scenario in scenarios if scenario.id == "vector.add"]
        self.assertEqual(2, len(expanded))


class CliTests(unittest.TestCase):
    def test_fail_fast_only_aborts_infrastructure_failures(self) -> None:
        for result in ("SETUP_FAILED", "RUNTIME_FAILED", "CASE_TIMEOUT"):
            self.assertTrue(should_abort(True, result))
        for result in ("PASS", "BLOCKED_BY_A_ALLREDUCE", "UNSUPPORTED"):
            self.assertFalse(should_abort(True, result))
        self.assertFalse(should_abort(False, "SETUP_FAILED"))

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

    def test_dry_run_lists_round2_with_fail_fast(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "run-xccl-allreduce-x-matrix.py"),
                "--config",
                str(ROUND2_EXPLORATION),
                "--dry-run",
                "--fail-fast",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("TOTAL=50", completed.stdout)


class BuilderTests(unittest.TestCase):
    def test_grouped_matmul_explicitly_disables_axis_grouping(self) -> None:
        scenario = next(
            scenario
            for scenario in load_config(CONFIG)[0]
            if scenario.id == "fused.grouped_matmul"
        )
        captured: dict = {}

        def grouped_matmul(xs, weights, **kwargs):
            captured.update(kwargs)
            return [object()]

        fake_torch = SimpleNamespace(
            bfloat16=object(),
            randn=lambda *shape, **kwargs: object(),
            ops=SimpleNamespace(
                npu=SimpleNamespace(npu_grouped_matmul=grouped_matmul)
            ),
        )
        operation = build_operation(fake_torch, object(), scenario, "npu:0")
        operation()
        self.assertEqual(-1, captured["group_type"])
        self.assertEqual(0, captured["split_item"])


if __name__ == "__main__":
    unittest.main()
