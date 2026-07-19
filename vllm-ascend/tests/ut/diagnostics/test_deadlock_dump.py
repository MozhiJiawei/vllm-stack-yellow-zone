from __future__ import annotations

import json
import signal
import sys
from pathlib import Path

import pytest

from vllm_ascend.diagnostics import deadlock_dump


@pytest.fixture(autouse=True)
def reset_diagnostics():
    deadlock_dump._reset_for_tests()
    yield
    deadlock_dump._reset_for_tests()


def test_disabled_does_nothing(monkeypatch: pytest.MonkeyPatch, tmp_path: Path):
    monkeypatch.delenv("VLLM_ASCEND_DEADLOCK_DIAG", raising=False)
    monkeypatch.setenv("VLLM_ASCEND_DEADLOCK_DIR", str(tmp_path))
    monkeypatch.setattr(deadlock_dump.faulthandler, "enable", lambda **kwargs: pytest.fail("unexpected enable"))

    assert deadlock_dump.initialize_deadlock_diagnostics(rank=0, local_rank=0) is None
    assert not list(tmp_path.iterdir())


def test_enabled_writes_metadata_and_registers_signal(monkeypatch: pytest.MonkeyPatch, tmp_path: Path):
    calls: list[tuple[str, object]] = []
    monkeypatch.setenv("VLLM_ASCEND_DEADLOCK_DIAG", "true")
    monkeypatch.setenv("VLLM_ASCEND_DEADLOCK_MODEL_ID", "A/../../unsafe")
    monkeypatch.setenv("VLLM_ASCEND_DEADLOCK_RUN_ID", "run 001")
    monkeypatch.setenv("VLLM_ASCEND_DEADLOCK_DIR", str(tmp_path))
    monkeypatch.setattr(deadlock_dump, "_proc_starttime", lambda pid: 123456)
    monkeypatch.setattr(deadlock_dump.signal, "SIGUSR1", getattr(signal, "SIGUSR1", signal.SIGTERM), raising=False)
    monkeypatch.setattr(
        deadlock_dump.faulthandler,
        "enable",
        lambda *, file, all_threads: calls.append(("enable", all_threads)),
    )
    monkeypatch.setattr(
        deadlock_dump.faulthandler,
        "register",
        lambda sig, *, file, all_threads, chain: calls.append(("register", (sig, all_threads, chain))),
        raising=False,
    )
    monkeypatch.setattr(deadlock_dump.faulthandler, "unregister", lambda sig: True, raising=False)

    path = deadlock_dump.initialize_deadlock_diagnostics(rank=3, local_rank=3)

    assert path == tmp_path / "run_001" / "model-A_.._.._unsafe" / "rank-3.json"
    metadata = json.loads(path.read_text(encoding="utf-8"))
    assert metadata["model"] == "A_.._.._unsafe"
    assert metadata["rank"] == 3
    assert metadata["local_rank"] == 3
    assert metadata["pid_starttime"] == 123456
    assert Path(metadata["stack_file"]).exists()
    assert calls == [("enable", True), ("register", (signal.SIGUSR1, True, False))]

    assert deadlock_dump.initialize_deadlock_diagnostics(rank=7, local_rank=7) == path
    assert len(calls) == 2


@pytest.mark.skipif(sys.platform != "linux", reason="requires Linux /proc")
def test_proc_starttime_matches_linux_stat():
    assert deadlock_dump._proc_starttime(1) > 0
