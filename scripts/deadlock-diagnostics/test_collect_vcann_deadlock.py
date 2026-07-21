from __future__ import annotations

import importlib.util
import json
import struct
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).with_name("collect_vcann_deadlock.py")
SPEC = importlib.util.spec_from_file_location("collect_vcann_deadlock", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


def test_parse_environ_keeps_only_diagnostic_fields():
    result = collector.parse_environ(
        b"LOCAL_RANK=3\0ENPU_DEADLOCK_TRACE=1\0HCCL_PORT=1234\0SECRET_TOKEN=nope\0"
    )
    assert result == {
        "LOCAL_RANK": "3",
        "ENPU_DEADLOCK_TRACE": "1",
        "HCCL_PORT": "1234",
    }


def test_explicit_pid_must_have_vcann_loaded(monkeypatch):
    monkeypatch.setattr(collector, "loaded_vcann", lambda pid: False)
    with pytest.raises(collector.CollectionError, match="has not loaded"):
        collector.select_targets([123], 1)


def test_expected_count_reports_candidates(monkeypatch):
    monkeypatch.setattr(
        collector,
        "discover_processes",
        lambda include_no_device=False: [
            {"pid": 10, "rank_hint": "0", "state": "S", "command": "worker"}
        ],
    )
    with pytest.raises(collector.CollectionError, match="expected 8"):
        collector.select_targets([], 8)


def test_summarize_vcann_sync_scene(tmp_path: Path):
    capture = tmp_path / "model-A"
    capture.mkdir()
    target = {
        "pid": 123,
        "rank_hint": "0",
        "command": "python worker.py",
    }
    (capture / "targets.json").write_text(
        json.dumps({"model": "A", "targets": [target]}), encoding="utf-8"
    )
    target_dir = collector.target_directory(capture, target)
    target_dir.mkdir()
    (target_dir / "vcann-trace.json").write_text(
        json.dumps(
            {
                "available": True,
                "enabled": True,
                "sync_probe": {
                    "active": True,
                    "stream": "0x1234",
                    "owner": 1,
                    "schedule_turn": 42,
                    "begin_sequence": 100,
                },
                "records": [
                    {
                        "sequence": 99,
                        "kind_name": "RT_KERNEL_HANDLE",
                        "object_symbol": "allreduce_bfloat16",
                        "on_sync_stream": True,
                    },
                    {
                        "sequence": 100,
                        "kind_name": "SCHED_SYNC_BEGIN",
                        "on_sync_stream": True,
                    },
                ],
            }
        ),
        encoding="utf-8",
    )

    rows = collector.summarize_dirs([capture], tmp_path / "summary")

    assert rows[0]["sync_active"] is True
    assert rows[0]["sync_stream"] == "0x1234"
    assert rows[0]["last_sequence"] == 99
    assert rows[0]["last_object"] == "allreduce_bfloat16"
    assert rows[0]["last_attempt_before_sync_sequence"] == 99
    assert rows[0]["first_attempt_after_sync_sequence"] is None
    assert (tmp_path / "summary" / "vcann-summary.csv").exists()


def test_parser_accepts_explicit_pid_capture():
    args = collector.build_parser().parse_args(
        [
            "capture",
            "--model",
            "A",
            "--output",
            "/tmp/capture",
            "--pid",
            "123",
            "--pid",
            "456",
            "--expected-processes",
            "2",
        ]
    )
    assert args.pid == [123, 456]
    assert args.expected_processes == 2
    assert args.trace_limit == 4096
    assert args.qwen_layers == 64


def test_pythonless_gdb_command_exports_every_fixed_abi_object(tmp_path: Path):
    command = collector.build_gdb_capture_command(
        "/usr/bin/gdb", 123, tmp_path, 4096, python_supported=False
    )
    expressions = [command[index + 1] for index, item in enumerate(command[:-1]) if item == "-ex"]
    dumps = [expression for expression in expressions if expression.startswith("dump binary memory")]

    assert len(dumps) == 4
    assert any("g_vcann_trace" in expression for expression in dumps)
    assert any("g_vcann_sync_probe" in expression for expression in dumps)
    assert any("g_vcann_host_sync_probe" in expression for expression in dumps)
    assert any("g_vcann_kernel_registry" in expression for expression in dumps)


def test_decode_raw_trace_without_gdb_python():
    size = (
        collector.TRACE_HEADER.size
        + collector.TRACE_CAPACITY * 4
        + collector.TRACE_CAPACITY * collector.TRACE_RECORD.size
    )
    trace = bytearray(size)
    collector.TRACE_HEADER.pack_into(
        trace,
        0,
        collector.TRACE_MAGIC,
        collector.TRACE_ABI_VERSION,
        collector.TRACE_CAPACITY,
        1,
        123,
        1,
    )
    records_offset = collector.TRACE_HEADER.size + collector.TRACE_CAPACITY * 4
    collector.TRACE_RECORD.pack_into(
        trace,
        records_offset,
        1,
        99,
        0x1234,
        0x5678,
        0,
        42,
        14,
        8,
        64,
        777,
    )
    probe = struct.pack("<IIiIQQQ", 1, 3, 1, 9, 100, 1, 0x1234)
    host_probe = bytes(collector.HOST_SYNC_PROBE.size)
    registry = bytearray(
        collector.KERNEL_REGISTRY_HEADER.size
        + collector.KERNEL_REGISTRY_CAPACITY * collector.KERNEL_REGISTRATION.size
    )
    collector.KERNEL_REGISTRY_HEADER.pack_into(
        registry,
        0,
        collector.KERNEL_REGISTRY_MAGIC,
        collector.KERNEL_REGISTRY_ABI_VERSION,
        collector.KERNEL_REGISTRY_CAPACITY,
        1,
        0,
        0,
    )
    collector.KERNEL_REGISTRATION.pack_into(
        registry,
        collector.KERNEL_REGISTRY_HEADER.size,
        1,
        0x5678,
        0x2222,
        0x3333,
        0,
        777,
        b"allreduce_bfloat16_t_7_mix_aiv",
        b"allreduce_bfloat16_t_7_mix_aiv",
    )

    result = collector.decode_raw_trace(
        bytes(trace), probe, host_probe, bytes(registry), 4096
    )

    assert result["available"] is True
    assert result["decoder"] == "raw-memory"
    assert result["sync_probe"]["active"] is True
    assert result["sync_probe"]["stream"] == "0x1234"
    assert result["records"][0]["kind_name"] == "RT_VECTOR_HANDLE"
    assert result["records"][0]["on_sync_stream"] is True
    assert result["records"][0]["kernel_names"] == [
        "allreduce_bfloat16_t_7_mix_aiv"
    ]


def test_qwen_state_machine_recovers_layer_and_phase_after_completed_sync():
    records = [
        {"sequence": 1, "kind_name": "DEVICE_SYNC_END", "stream": "0x0"},
        {
            "sequence": 2,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["matmul_bfloat16_t_1"],
        },
        {
            "sequence": 3,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["flash_attention_bfloat16_t_1_mix_aic"],
        },
        {
            "sequence": 4,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["matmul_bfloat16_t_1"],
        },
        {
            "sequence": 5,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["allreduce_bfloat16_t_7_mix_aiv"],
        },
        {
            "sequence": 6,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["matmul_bfloat16_t_1"],
        },
        {
            "sequence": 7,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["matmul_bfloat16_t_1"],
        },
        {
            "sequence": 8,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_names": ["allreduce_bfloat16_t_7_mix_aiv"],
        },
    ]

    collector.annotate_qwen_states(records, 64)

    assert records[1]["qwen_layer"] == 0
    assert records[1]["qwen_phase"] == "ATTENTION_QKV"
    assert records[3]["qwen_phase"] == "ATTENTION_O"
    assert records[4]["qwen_phase"] == "ATTENTION_TP_ALLREDUCE"
    assert records[5]["qwen_phase"] == "MLP_UP_GATE"
    assert records[6]["qwen_phase"] == "MLP_DOWN"
    assert records[7]["qwen_phase"] == "MLP_TP_ALLREDUCE"
    assert records[7]["qwen_state_basis"] == "after_completed_device_sync"


def test_blocked_scheduler_sync_selects_first_kernel_after_completion_boundary():
    records = [
        {"sequence": 10, "kind_name": "SCHED_SYNC_END", "stream": "0x1234"},
        {
            "sequence": 11,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_name": "allreduce_bfloat16_t",
            "qwen_layer": 7,
            "qwen_phase": "ATTENTION_TP_ALLREDUCE",
        },
        {
            "sequence": 12,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "stream": "0x1234",
            "kernel_name": "matmul_bfloat16_t",
        },
    ]
    trace = {"sync_probe": {"active": True, "stream": "0x1234", "begin_sequence": 13}}

    result = collector.blocked_kernel_candidate(trace, records)

    assert result["sequence"] == 11
    assert result["qwen_layer"] == 7
    assert result["qwen_phase"] == "ATTENTION_TP_ALLREDUCE"
    assert "last successful" in result["completion_evidence"]
    assert result["unresolved_kernel_count"] == 2
    assert "11:allreduce_bfloat16_t:7:ATTENTION_TP_ALLREDUCE" in result[
        "unresolved_kernel_window"
    ]
    assert result["unresolved_collectives"].startswith("11:allreduce_bfloat16_t")


def test_blocked_device_sync_limits_window_to_work_after_last_completed_sync():
    records = [
        {
            "sequence": 1,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "kernel_name": "warmup_kernel",
        },
        {"sequence": 2, "kind_name": "DEVICE_SYNC_END"},
        {
            "sequence": 3,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "kernel_name": "matmul_bfloat16_t",
        },
        {
            "sequence": 4,
            "kind_name": "RT_KERNEL_HANDLE_V2",
            "kernel_name": "allreduce_bfloat16_t",
        },
    ]
    trace = {"host_sync_probe": {"active": True, "begin_sequence": 5}}

    result = collector.blocked_kernel_candidate(trace, records)

    assert result["sequence"] == 4
    assert result["unresolved_kernel_count"] == 2
    assert "warmup_kernel" not in result["unresolved_kernel_window"]
    assert "last submitted" in result["completion_evidence"]
