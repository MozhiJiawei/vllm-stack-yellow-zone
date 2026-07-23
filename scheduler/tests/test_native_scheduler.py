from __future__ import annotations

import os
import multiprocessing
import struct
import threading
import time
from contextlib import ExitStack
from dataclasses import replace
from pathlib import Path

import pytest

from vllm_pair_scheduler import (
    PairSchedulerConfig,
    PairSchedulerError,
    PairSchedulerFailed,
    PairSchedulerTimeout,
    SharedMemoryForwardGate,
)
from vllm_pair_scheduler.inspect import exit_code, inspect_pair

pytestmark = pytest.mark.skipif(os.name != "posix", reason="Linux futex test")


def _primary_until_killed(path: str, ready) -> None:
    with SharedMemoryForwardGate(
        config(Path(path), "primary", "A", peer_timeout_ms=80)
    ):
        ready.send("ready")
        ready.close()
        while True:
            time.sleep(1)


def _standby_owner_then_exit(path: str, ready) -> None:
    gate = SharedMemoryForwardGate(
        config(Path(path), "standby", "B", peer_timeout_ms=80)
    )
    gate.enter_forward()
    ready.send("owner")
    ready.close()
    os._exit(17)


def config(
    path: Path,
    role: str,
    instance: str,
    *,
    pair: str = "test-pair",
    forward_timeout_ms: int = 2_000,
    peer_timeout_ms: int = 1_000,
) -> PairSchedulerConfig:
    return PairSchedulerConfig(
        mode="elastic",
        role=role,
        instance_id=instance,
        pair_id=pair,
        shm_dir=path,
        init_timeout_ms=600,
        forward_timeout_ms=forward_timeout_ms,
        heartbeat_ms=10,
        peer_timeout_ms=peer_timeout_ms,
    )


def open_pair(
    stack: ExitStack, path: Path, worker_count: int
) -> tuple[list[SharedMemoryForwardGate], list[SharedMemoryForwardGate]]:
    primary_cfg = config(path, "primary", "A")
    standby_cfg = config(path, "standby", "B")
    a = [
        stack.enter_context(
            SharedMemoryForwardGate(
                primary_cfg, worker_rank=rank, worker_count=worker_count
            )
        )
        for rank in range(worker_count)
    ]
    b = [
        stack.enter_context(
            SharedMemoryForwardGate(
                standby_cfg, worker_rank=rank, worker_count=worker_count
            )
        )
        for rank in range(worker_count)
    ]
    return a, b


def run_worker_round(
    gates: list[SharedMemoryForwardGate],
    *,
    duration: float = 0.0001,
) -> tuple[set[int], list[tuple[int, int]]]:
    grants: set[int] = set()
    intervals: list[tuple[int, int]] = []
    errors: list[BaseException] = []
    lock = threading.Lock()

    def worker(gate: SharedMemoryForwardGate) -> None:
        try:
            sequence, grant = gate.enter_forward()
            start = time.monotonic_ns()
            time.sleep(duration)
            end = time.monotonic_ns()
            gate.leave_forward(sequence, grant)
            with lock:
                grants.add(grant)
                intervals.append((start, end))
        except BaseException as exc:
            errors.append(exc)

    threads = [threading.Thread(target=worker, args=(gate,)) for gate in gates]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=5)
        assert not thread.is_alive()
    assert not errors
    return grants, intervals


def test_double_primary_is_rejected(tmp_path: Path) -> None:
    with SharedMemoryForwardGate(config(tmp_path, "primary", "A")):
        with pytest.raises(PairSchedulerError, match="another primary"):
            SharedMemoryForwardGate(config(tmp_path, "primary", "A"))


def test_standby_can_wait_for_primary(tmp_path: Path) -> None:
    result: list[SharedMemoryForwardGate] = []

    def attach() -> None:
        result.append(SharedMemoryForwardGate(config(tmp_path, "standby", "B")))

    thread = threading.Thread(target=attach)
    thread.start()
    time.sleep(0.05)
    with SharedMemoryForwardGate(config(tmp_path, "primary", "A")):
        thread.join(timeout=2)
        assert len(result) == 1
        result[0].close()


def test_v2_layout_is_rejected(tmp_path: Path) -> None:
    with SharedMemoryForwardGate(config(tmp_path, "primary", "A")):
        shm_path = next(tmp_path.glob("*.shm"))
        with shm_path.open("r+b", buffering=0) as shared:
            shared.seek(8)
            shared.write(struct.pack("I", 2))
        with pytest.raises(PairSchedulerTimeout, match="protocol mismatch"):
            SharedMemoryForwardGate(config(tmp_path, "standby", "B"))


def test_configuration_and_worker_count_mismatch(tmp_path: Path) -> None:
    with SharedMemoryForwardGate(
        config(tmp_path, "primary", "A"), worker_count=2
    ):
        with pytest.raises(PairSchedulerTimeout, match="configuration mismatch"):
            SharedMemoryForwardGate(
                config(tmp_path, "standby", "B"), worker_count=1
            )
        mismatched = replace(
            config(tmp_path, "standby", "B"), heartbeat_ms=20
        )
        with pytest.raises(PairSchedulerTimeout, match="configuration mismatch"):
            SharedMemoryForwardGate(
                mismatched, worker_rank=0, worker_count=2
            )


@pytest.mark.parametrize("worker_count", [1, 2, 4])
def test_one_grant_is_consumed_by_all_workers(
    tmp_path: Path, worker_count: int
) -> None:
    with ExitStack() as stack:
        a, _ = open_pair(stack, tmp_path, worker_count)
        grants, _ = run_worker_round(a)
        assert len(grants) == 1
        snapshot = inspect_pair("test-pair", tmp_path)
        assert snapshot["protocol_version"] == 3
        assert snapshot["instances"]["A"]["state"] == "IDLE"
        assert snapshot["instances"]["A"]["ready_mask"] == 0
        assert snapshot["instances"]["A"]["complete_mask"] == 0


def test_inspector_reports_worker_registration(tmp_path: Path) -> None:
    with ExitStack() as stack:
        a, b = open_pair(stack, tmp_path, 4)
        snapshot = inspect_pair("test-pair", tmp_path)
        assert snapshot["worker_count"] == 4
        assert snapshot["instances"]["A"]["registration_complete"]
        assert snapshot["instances"]["B"]["registration_complete"]
        assert snapshot["instances"]["A"]["registration_mask"] == 0b1111
        assert all(worker["online"] for worker in snapshot["instances"]["A"]["workers"])
        assert exit_code(snapshot) == 0
        assert len(a) == len(b) == 4


def test_duplicate_live_rank_is_rejected(tmp_path: Path) -> None:
    cfg = config(tmp_path, "primary", "A")
    with SharedMemoryForwardGate(cfg, worker_rank=0, worker_count=2):
        with pytest.raises(PairSchedulerError, match="another primary"):
            SharedMemoryForwardGate(cfg, worker_rank=0, worker_count=2)


def test_duplicate_nonleader_rank_is_rejected_by_protocol(tmp_path: Path) -> None:
    cfg = config(tmp_path, "primary", "A")
    leader = SharedMemoryForwardGate(cfg, worker_rank=0, worker_count=2)
    follower = SharedMemoryForwardGate(cfg, worker_rank=1, worker_count=2)
    try:
        with pytest.raises(PairSchedulerTimeout, match="already active"):
            SharedMemoryForwardGate(cfg, worker_rank=1, worker_count=2)
    finally:
        follower.close()
        leader.close()


def test_a_and_b_forwards_never_overlap_tp4(tmp_path: Path) -> None:
    records: list[tuple[str, int, int]] = []
    errors: list[BaseException] = []
    lock = threading.Lock()
    with ExitStack() as stack:
        a, b = open_pair(stack, tmp_path, 4)

        def worker(instance: str, gate: SharedMemoryForwardGate) -> None:
            try:
                for _ in range(20):
                    sequence, grant = gate.enter_forward()
                    start = time.monotonic_ns()
                    time.sleep(0.0002)
                    end = time.monotonic_ns()
                    gate.leave_forward(sequence, grant)
                    with lock:
                        records.append((instance, start, end))
            except BaseException as exc:
                errors.append(exc)

        threads = [
            threading.Thread(target=worker, args=("A", gate)) for gate in a
        ] + [threading.Thread(target=worker, args=("B", gate)) for gate in b]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=20)
            assert not thread.is_alive()
        assert not errors

    a_intervals = [(start, end) for name, start, end in records if name == "A"]
    b_intervals = [(start, end) for name, start, end in records if name == "B"]
    assert len(a_intervals) == len(b_intervals) == 80
    assert all(
        a_end <= b_start or b_end <= a_start
        for a_start, a_end in a_intervals
        for b_start, b_end in b_intervals
    )


def test_sampling_can_overlap_peer_forward(tmp_path: Path) -> None:
    events: dict[str, int] = {}
    with ExitStack() as stack:
        a, b = open_pair(stack, tmp_path, 1)
        b_waiting = threading.Event()

        def standby() -> None:
            b_waiting.set()
            sequence, grant = b[0].enter_forward()
            events["b_forward_start"] = time.monotonic_ns()
            time.sleep(0.03)
            events["b_forward_end"] = time.monotonic_ns()
            b[0].leave_forward(sequence, grant)

        sequence, grant = a[0].enter_forward()
        thread = threading.Thread(target=standby)
        thread.start()
        b_waiting.wait()
        time.sleep(0.01)
        a[0].leave_forward(sequence, grant)
        events["a_sampling_start"] = time.monotonic_ns()
        time.sleep(0.03)
        events["a_sampling_end"] = time.monotonic_ns()
        thread.join(timeout=2)

    assert events["b_forward_start"] < events["a_sampling_end"]
    assert events["a_sampling_start"] < events["b_forward_end"]


def test_missing_rank_fails_barrier_closed(tmp_path: Path) -> None:
    cfg = config(tmp_path, "primary", "A", peer_timeout_ms=80)
    gates = [
        SharedMemoryForwardGate(cfg, worker_rank=rank, worker_count=4)
        for rank in range(3)
    ]
    errors: list[BaseException] = []
    try:
        threads = [
            threading.Thread(
                target=lambda gate=gate: _capture_enter(gate, errors)
            )
            for gate in gates
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=2)
            assert not thread.is_alive()
        assert errors
        assert inspect_pair("test-pair", tmp_path)["failure_reason"] == 112
    finally:
        for gate in reversed(gates):
            gate.close()


def _capture_enter(
    gate: SharedMemoryForwardGate, errors: list[BaseException]
) -> None:
    try:
        gate.enter_forward()
    except BaseException as exc:
        errors.append(exc)


def test_forward_timeout_and_late_complete_fail_closed(tmp_path: Path) -> None:
    cfg = config(tmp_path, "primary", "A", forward_timeout_ms=40)
    with SharedMemoryForwardGate(cfg) as gate:
        sequence, grant = gate.enter_forward()
        time.sleep(0.07)
        with pytest.raises(PairSchedulerFailed):
            gate.leave_forward(sequence, grant)
        snapshot = inspect_pair("test-pair", tmp_path)
        assert snapshot["state"] == "FAILED"
        assert snapshot["failure_reason"] == 102


def test_cross_round_completion_is_fenced(tmp_path: Path) -> None:
    gate = SharedMemoryForwardGate(config(tmp_path, "primary", "A"))
    try:
        sequence, grant = gate.enter_forward()
        with pytest.raises(PairSchedulerFailed, match="fencing failed"):
            gate.leave_forward(sequence + 1, grant)
        assert inspect_pair("test-pair", tmp_path)["failure_reason"] == 110
    finally:
        gate.close()


def test_corrupt_owner_is_detected_fail_closed(tmp_path: Path) -> None:
    gate = SharedMemoryForwardGate(config(tmp_path, "primary", "A"))
    try:
        shm_path = next(tmp_path.glob("*.shm"))
        with shm_path.open("r+b", buffering=0) as shared:
            shared.seek(48)
            shared.write(struct.pack("i", 99))
        deadline = time.monotonic() + 1
        while inspect_pair("test-pair", tmp_path)["state"] != "FAILED":
            assert time.monotonic() < deadline
            time.sleep(0.01)
        assert inspect_pair("test-pair", tmp_path)["failure_reason"] == 108
    finally:
        gate.close()


def test_owner_worker_close_fails_pair(tmp_path: Path) -> None:
    primary = SharedMemoryForwardGate(config(tmp_path, "primary", "A"))
    standby = SharedMemoryForwardGate(config(tmp_path, "standby", "B"))
    standby.enter_forward()
    standby.close()
    with pytest.raises(PairSchedulerFailed):
        primary.enter_forward()
    primary.close()


def test_sigkill_primary_wakes_waiting_standby(tmp_path: Path) -> None:
    context = multiprocessing.get_context("spawn")
    receiver, sender = context.Pipe(duplex=False)
    process = context.Process(
        target=_primary_until_killed, args=(str(tmp_path), sender)
    )
    process.start()
    assert receiver.recv() == "ready"
    standby = SharedMemoryForwardGate(
        config(tmp_path, "standby", "B", peer_timeout_ms=80)
    )
    process.kill()
    process.join(timeout=2)
    assert process.exitcode is not None
    with pytest.raises(PairSchedulerFailed, match="heartbeat expired"):
        standby.enter_forward()
    standby.close()


def test_sigkill_owner_worker_fails_pair(tmp_path: Path) -> None:
    context = multiprocessing.get_context("spawn")
    primary = SharedMemoryForwardGate(
        config(tmp_path, "primary", "A", peer_timeout_ms=80)
    )
    receiver, sender = context.Pipe(duplex=False)
    process = context.Process(
        target=_standby_owner_then_exit, args=(str(tmp_path), sender)
    )
    process.start()
    assert receiver.recv() == "owner"
    process.join(timeout=2)
    assert process.exitcode == 17
    with pytest.raises(PairSchedulerFailed):
        primary.enter_forward()
    assert inspect_pair("test-pair", tmp_path)["failure_reason"] == 104
    primary.close()


def test_clean_idle_worker_restart_gets_new_session(tmp_path: Path) -> None:
    with SharedMemoryForwardGate(config(tmp_path, "primary", "A")):
        standby = SharedMemoryForwardGate(config(tmp_path, "standby", "B"))
        first = inspect_pair("test-pair", tmp_path)["instances"]["B"]["workers"][0][
            "session"
        ]
        standby.close()
        with SharedMemoryForwardGate(config(tmp_path, "standby", "B")):
            second = inspect_pair("test-pair", tmp_path)["instances"]["B"][
                "workers"
            ][0]["session"]
            assert first != second


def test_idle_worker_restart_continues_sequence(tmp_path: Path) -> None:
    primary = SharedMemoryForwardGate(config(tmp_path, "primary", "A"))
    standby = SharedMemoryForwardGate(config(tmp_path, "standby", "B"))
    try:
        sequence, grant = standby.enter_forward()
        standby.leave_forward(sequence, grant)
        standby.close()
        standby = SharedMemoryForwardGate(config(tmp_path, "standby", "B"))
        next_sequence, next_grant = standby.enter_forward()
        standby.leave_forward(next_sequence, next_grant)
        assert next_sequence == sequence + 1
    finally:
        standby.close()
        primary.close()


def test_primary_close_removes_generation(tmp_path: Path) -> None:
    primary = SharedMemoryForwardGate(config(tmp_path, "primary", "A"))
    primary.close()
    assert not list(tmp_path.glob("*.current"))
    assert not list(tmp_path.glob("*.shm"))


def test_explicit_forward_failure_fails_both_instances(tmp_path: Path) -> None:
    with ExitStack() as stack:
        a, b = open_pair(stack, tmp_path, 1)
        sequence, grant = a[0].enter_forward()
        assert sequence == 1 and grant > 0
        a[0].fail(201)
        with pytest.raises(PairSchedulerFailed):
            b[0].enter_forward()


def test_invalid_worker_bounds_are_rejected(tmp_path: Path) -> None:
    cfg = config(tmp_path, "primary", "A")
    with pytest.raises(ValueError, match="between 1 and 64"):
        SharedMemoryForwardGate(cfg, worker_count=65)
    with pytest.raises(ValueError, match="identify a local worker"):
        SharedMemoryForwardGate(cfg, worker_rank=4, worker_count=4)


def test_stress_100k_instance_grants(tmp_path: Path) -> None:
    grants: set[int] = set()
    errors: list[BaseException] = []
    lock = threading.Lock()
    with ExitStack() as stack:
        a, b = open_pair(stack, tmp_path, 1)
        start = threading.Barrier(2)

        def run(gate: SharedMemoryForwardGate) -> None:
            local: list[int] = []
            try:
                start.wait()
                for _ in range(50_000):
                    sequence, grant = gate.enter_forward()
                    gate.leave_forward(sequence, grant)
                    local.append(grant)
                with lock:
                    grants.update(local)
            except BaseException as exc:
                errors.append(exc)

        threads = [
            threading.Thread(target=run, args=(a[0],)),
            threading.Thread(target=run, args=(b[0],)),
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=60)
            assert not thread.is_alive()
        assert not errors
        assert len(grants) == 100_000
        assert inspect_pair("test-pair", tmp_path)["next_grant_id"] == 100_000
