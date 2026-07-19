from __future__ import annotations

import importlib.util
import json
import os
import sys
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).with_name("deadlock_snapshot.py")
SPEC = importlib.util.spec_from_file_location("deadlock_snapshot", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
deadlock_snapshot = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deadlock_snapshot)


@pytest.mark.skipif(sys.platform != "linux", reason="requires Linux /proc")
def test_proc_starttime_for_current_process():
    assert deadlock_snapshot.proc_starttime(os.getpid()) > 0


@pytest.mark.parametrize(
    ("raw", "expected"),
    [
        ("0", [0]),
        ("0,100,500,2000", [0, 100, 500, 2000]),
    ],
)
def test_parse_delays(raw, expected):
    assert deadlock_snapshot.parse_delays(raw) == expected


@pytest.mark.parametrize("raw", ["100", "0,-1", "0,500,100", "zero"])
def test_parse_delays_rejects_invalid_values(raw):
    with pytest.raises(Exception):
        deadlock_snapshot.parse_delays(raw)


@pytest.mark.parametrize("value", ["../run", ".", "run/id", "run id", ""])
def test_validate_identifier_rejects_unsafe_paths(value):
    with pytest.raises(deadlock_snapshot.CollectionError):
        deadlock_snapshot.validate_identifier("run ID", value)


def test_validate_identifier_accepts_normal_run_id():
    deadlock_snapshot.validate_identifier("run ID", "run-001.test")


@pytest.mark.parametrize(
    ("wchan", "syscall", "native", "expected"),
    [
        ("", "", "libxlite.so!xcclAllReduce", "XCCL_COLLECTIVE_WAIT"),
        ("", "", "aclrtSynchronizeStream", "NPU_STREAM_SYNC_WAIT"),
        ("do_vfs_ioctl", "ioctl", "", "NPU_RUNTIME_IOCTL_WAIT"),
        ("futex_wait_queue", "futex", "", "HOST_BARRIER_WAIT"),
        ("ep_poll", "epoll_wait", "", "RPC_OR_QUEUE_WAIT"),
        ("", "", "0x1234", "UNKNOWN_NATIVE_ADDRESS"),
    ],
)
def test_classify(wchan, syscall, native, expected):
    assert deadlock_snapshot.classify(wchan, syscall, native) == expected


def test_classify_uses_python_xlite_frame():
    assert (
        deadlock_snapshot.classify("", "", "", "xlite_model.forward")
        == "XLITE_FORWARD_WAIT"
    )


def test_parser_accepts_targeted_trace_command():
    args = deadlock_snapshot.build_parser().parse_args(
        [
            "trace",
            "--run-id",
            "run-001",
            "--model",
            "A",
            "--rank",
            "0",
            "--rank",
            "4",
            "--output",
            "/tmp/trace",
        ]
    )
    assert args.rank == [0, 4]
    assert args.duration == 2.0


def test_summarize_writes_csv_and_json(tmp_path: Path):
    capture = tmp_path / "model-A"
    sample_dir = capture / "samples"
    sample_dir.mkdir(parents=True)
    worker = {"model": "A", "rank": 0, "npu": 0, "pid": 123, "pid_starttime": 456}
    (capture / "manifest.json").write_text(
        json.dumps({"workers": [worker]}), encoding="utf-8"
    )
    process = {
        "model": "A",
        "rank": 0,
        "pid": 123,
        "threads": [
            {
                "tid": 123,
                "comm": "worker",
                "wchan": "futex_wait",
                "syscall": "futex(...)",
            }
        ],
    }
    for sample_id in range(2):
        (sample_dir / f"sample-{sample_id:02d}.json").write_text(
            json.dumps({"processes": [process]}), encoding="utf-8"
        )
    rank_dir = capture / "rank-0"
    rank_dir.mkdir()
    (rank_dir / "python-stacks.log").write_text(
        '# collector sample=1 send_monotonic_ns=1\nCurrent thread 0x1:\n  File "worker.py", line 7 in run\n',
        encoding="utf-8",
    )
    (rank_dir / "gdb-native-bt.txt").write_text(
        "Thread 1\n#0  futex_wait () from libc.so\n#1 worker_loop ()\n",
        encoding="utf-8",
    )

    rows = deadlock_snapshot.summarize_dirs([capture], tmp_path / "summary")

    assert rows[0]["classification"] == "HOST_BARRIER_WAIT"
    assert rows[0]["stable_samples"] == "2/2"
    assert "worker.py" in rows[0]["python_top"]
    assert rows[0]["native_top"].startswith("#0")
    assert (tmp_path / "summary" / "rank-summary.csv").exists()
    assert (tmp_path / "summary" / "rank-summary.json").exists()
