from __future__ import annotations

import importlib.util
import json
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
