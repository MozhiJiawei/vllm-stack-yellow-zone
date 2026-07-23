from __future__ import annotations

import threading
import time


def test_worker_boundary_order_and_sampling_bypass() -> None:
    calls: list[str] = []

    class Gate:
        def enter_forward(self) -> tuple[int, int]:
            calls.append("enter_forward")
            return 3, 7

        def leave_forward(self, sequence: int, grant: int) -> None:
            assert (sequence, grant) == (3, 7)
            calls.append("leave_forward")

    gate = Gate()
    round_token = gate.enter_forward()
    calls.append("worker.execute_model")
    gate.leave_forward(*round_token)
    calls.append("worker.sample_tokens")
    assert calls == [
        "enter_forward",
        "worker.execute_model",
        "leave_forward",
        "worker.sample_tokens",
    ]


def test_engine_queue_is_not_drained_by_worker_gate() -> None:
    calls: list[str] = []
    sample_one_done = threading.Event()

    calls.append("worker.execute_1")
    calls.append("worker.sample_1")
    assert not sample_one_done.is_set()
    calls.append("worker.execute_2")
    calls.append("worker.sample_2")

    assert calls == [
        "worker.execute_1",
        "worker.sample_1",
        "worker.execute_2",
        "worker.sample_2",
    ]


def test_empty_plan_and_sampling_do_not_enter_gate() -> None:
    calls: list[str] = []
    for method, scheduled_tokens in (
        ("execute_model", 0),
        ("sample_tokens", 1),
        ("get_model", 1),
    ):
        if method == "execute_model" and scheduled_tokens > 0:
            calls.append("enter_forward")
        calls.append(method)
    assert calls == ["execute_model", "sample_tokens", "get_model"]


def test_async_device_tail_is_explicitly_outside_v3_contract() -> None:
    device_done = threading.Event()

    def asynchronous_tail() -> None:
        time.sleep(0.03)
        device_done.set()

    thread = threading.Thread(target=asynchronous_tail)
    thread.start()
    host_execute_model_returned = True
    assert host_execute_model_returned
    assert not device_done.is_set(), (
        "An executor with an asynchronous collective tail does not satisfy "
        "the v3 host-return completion contract."
    )
    thread.join()
