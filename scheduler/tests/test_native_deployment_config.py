from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREPARE = ROOT / "scripts" / "pair-scheduler" / "prepare-yellow-zone.sh"
RECREATE = (
    ROOT
    / "scripts"
    / "pair-scheduler"
    / "recreate-native-xlite-containers.sh"
)


def test_prepare_uses_native_container_entry_point() -> None:
    script = PREPARE.read_text(encoding="utf-8")

    assert "ROOT=${ROOT:-/root/l00933108/vllm-stack-yellow-zone}" in script
    assert "recreate-native-xlite-containers.sh" in script
    assert "restart-vcann-xlite-containers.sh" not in script
    assert 'wheel=$WHEEL' not in script
    assert "tasks exec" not in script
    assert "ARTIFACT_ROOT=${ARTIFACT_ROOT:-/root/l00933108/.artifacts}" in script
    assert 'ARTIFACT_DIR="$ROOT/.artifacts/pair-scheduler"' not in script


def test_native_container_inputs_are_pinned() -> None:
    script = RECREATE.read_text(encoding="utf-8")

    assert "quay.io/ascend/vllm-ascend:v0.19.1rc1" in script
    assert "xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl" in script
    assert (
        "cccb74688f6acb9cc219290c3a04b6005b81dba941b9d63c79bd52d02854fc8a"
        in script
    )
    assert "restart-vcann-xlite-containers.sh" not in script
    assert "runtime/vcann" not in script.lower()
    assert "tasks exec" not in script
