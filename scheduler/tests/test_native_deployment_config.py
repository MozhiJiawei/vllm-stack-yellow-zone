from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREPARE = ROOT / "scripts" / "pair-scheduler" / "prepare-yellow-zone.sh"
START = ROOT / "scripts" / "pair-scheduler" / "start-yellow-zone.sh"
AISBENCH = ROOT / "scripts" / "pair-scheduler" / "run-aisbench-yellow-zone.sh"
RECREATE = (
    ROOT
    / "scripts"
    / "pair-scheduler"
    / "recreate-native-xlite-containers.sh"
)
INSTALLER = ROOT / "scheduler" / "install-pair-scheduler.sh"
NATIVE = (
    ROOT
    / "scheduler"
    / "src"
    / "vllm_pair_scheduler"
    / "native"
    / "pair_sched.c"
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


def test_native_pair_start_reserves_memory_for_the_peer() -> None:
    script = START.read_text(encoding="utf-8")

    assert "GPU_MEMORY_UTILIZATION=${GPU_MEMORY_UTILIZATION:-0.35}" in script
    assert "--gpu-memory-utilization '$GPU_MEMORY_UTILIZATION'" in script


def test_native_start_supports_scheduler_free_single_instance_baseline() -> None:
    script = START.read_text(encoding="utf-8")

    assert "TARGETS=${TARGETS:-AB}" in script
    assert "REQUIRE_SCHEDULER=${REQUIRE_SCHEDULER:-1}" in script
    assert "if [[ $TARGETS == AB ]]" in script
    assert "if [[ $REQUIRE_SCHEDULER == 1 ]]" in script


def test_native_pair_start_isolates_host_and_npu_hccl_ports() -> None:
    script = START.read_text(encoding="utf-8")

    assert "export HCCL_HOST_SOCKET_PORT_RANGE='$socket_range'" in script
    assert "export HCCL_NPU_SOCKET_PORT_RANGE='$socket_range'" in script
    assert "HCCL_SOCKET_PORT_RANGE" not in script
    assert "61000-61050" in script
    assert "62000-62050" in script


def test_aisbench_uses_mindie_image_as_cpu_only_pair_client() -> None:
    script = AISBENCH.read_text(encoding="utf-8")

    assert "mindie:2.3.1.B020" in script
    assert "--net-host" in script
    assert (
        "src=/usr/local/Ascend/driver,dst=/usr/local/Ascend/driver,"
        "options=rbind:ro"
    ) in script
    assert "--device" not in script
    assert "run_client A 10040" in script
    assert "run_client B 10041" in script
    assert "synthetic_gen" in script
    assert (
        '"generation_kwargs = dict(": '
        '"generation_kwargs = dict(ignore_eos=True,"'
    ) in script
    assert "DRY_RUN=${DRY_RUN:-0}" in script
    assert "TARGETS=${TARGETS:-AB}" in script
    assert '[[ $TARGETS == A || $TARGETS == AB ]]' in script


def test_installer_requires_protocol_v4_sampling_patch() -> None:
    script = INSTALLER.read_text(encoding="utf-8")

    assert "vllm-pair-elastic-scheduling-v3-to-v4.patch" in script
    assert "worker.sample_tokens = gated_sample_tokens" in script
    assert "protocol=4" in script
    assert "protocol=3" not in script


def test_native_structured_logs_use_the_protocol_constant() -> None:
    source = NATIVE.read_text(encoding="utf-8")

    assert source.count('"\\"protocol\\":%u') == 2
    assert source.count("PS_VERSION,") >= 2
    assert '"\\"protocol\\":3' not in source
