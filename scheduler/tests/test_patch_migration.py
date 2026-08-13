from __future__ import annotations

import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXECUTOR_FILES = (
    "uniproc_executor.py",
    "multiproc_executor.py",
)


def _copy_base(destination: Path) -> None:
    target = destination / "vllm/v1/executor"
    target.mkdir(parents=True)
    source = ROOT / "vllm/vllm/v1/executor"
    for name in EXECUTOR_FILES:
        shutil.copy2(source / name, target / name)


def _apply(tree: Path, patch: Path, *, reverse: bool = False) -> None:
    command = ["git", "apply", "--ignore-space-change"]
    if reverse:
        command.append("--reverse")
    command.append(str(patch))
    subprocess.run(command, cwd=tree, check=True, capture_output=True, text=True)


def test_protocol_v3_patch_migrates_exactly_to_v4(tmp_path: Path) -> None:
    full_patch = ROOT / "patches/vllm-pair-elastic-scheduling.patch"
    migration_patch = (
        ROOT / "patches/vllm-pair-elastic-scheduling-v3-to-v4.patch"
    )
    expected = tmp_path / "expected"
    migrated = tmp_path / "migrated"
    _copy_base(expected)
    _apply(expected, full_patch)
    shutil.copytree(expected, migrated)

    _apply(migrated, migration_patch, reverse=True)
    for name in EXECUTOR_FILES:
        source = (migrated / "vllm/v1/executor" / name).read_text()
        assert "pair scheduling v3" in source
        assert "worker.sample_tokens = gated_sample_tokens" not in source

    _apply(migrated, migration_patch)
    for name in EXECUTOR_FILES:
        relative = Path("vllm/v1/executor") / name
        assert (migrated / relative).read_text() == (expected / relative).read_text()
