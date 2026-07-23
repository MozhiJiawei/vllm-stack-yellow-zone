from __future__ import annotations

import asyncio
import contextlib
import inspect
import os
import queue
import sys
import types
from collections import deque
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import pytest


def _load_vllm_classes():
    source = os.environ.get("VLLM_SOURCE_ROOT")
    if not source:
        pytest.skip("set VLLM_SOURCE_ROOT to the locked vLLM v0.19.1 tree")
    source_path = Path(source)
    if not (source_path / "vllm/v1/engine/core.py").is_file():
        pytest.fail(f"invalid VLLM_SOURCE_ROOT: {source_path}")
    sys.path.insert(0, str(source_path))
    os.environ["VLLM_PLUGINS"] = "none"
    if sys.platform == "win32" and "uvloop" not in sys.modules:
        sys.modules["uvloop"] = types.SimpleNamespace(run=asyncio.run)
    import vllm.platforms as platforms

    # The empty build has no device extension. Keep this control-flow test
    # independent of whichever accelerator happens to be visible on the host.
    platforms.builtin_platform_plugins["cuda"] = lambda: None
    from vllm.v1.engine.core import EngineCore
    from vllm.v1.executor.multiproc_executor import WorkerProc

    return EngineCore, WorkerProc


class FakeFuture:
    def __init__(
        self,
        trace: list[str],
        name: str,
        value: Any,
        *,
        ready: bool = False,
        error: BaseException | None = None,
    ):
        self.trace = trace
        self.name = name
        self.value = value
        self.ready = ready
        self.error = error

    def done(self) -> bool:
        return self.ready

    def result(self):
        self.trace.append(f"{self.name}.result")
        if self.error is not None:
            raise self.error
        self.ready = True
        return self.value


class FakeScheduler:
    def __init__(self, outputs: list[Any], trace: list[str]):
        self.outputs = deque(outputs)
        self.trace = trace

    def has_requests(self) -> bool:
        return bool(self.outputs)

    def schedule(self):
        output = self.outputs.popleft()
        self.trace.append(f"schedule:{output.name}")
        return output

    def get_grammar_bitmask(self, output):
        self.trace.append(f"grammar:{output.name}")
        return output.name

    def update_from_output(self, output, model_output):
        self.trace.append(f"update:{output.name}:{model_output}")
        return {0: model_output}


class FakeExecutor:
    def __init__(self, trace: list[str], *, sampling_error: bool = False):
        self.trace = trace
        self.sampling_error = sampling_error

    def execute_model(self, output, non_block=False):
        assert non_block
        self.trace.append(f"execute:{output.name}")
        return FakeFuture(
            self.trace, f"execute:{output.name}", f"forward-{output.name}"
        )

    def sample_tokens(self, grammar, non_block=False):
        assert non_block
        self.trace.append(f"sample:{grammar}")
        return FakeFuture(
            self.trace,
            f"sample:{grammar}",
            f"token-{grammar}",
            error=RuntimeError("sampling failed") if self.sampling_error else None,
        )


def _engine(outputs: list[Any], trace: list[str], *, sampling_error: bool = False):
    EngineCore, _ = _load_vllm_classes()
    engine = EngineCore.__new__(EngineCore)
    engine.scheduler = FakeScheduler(outputs, trace)
    engine.model_executor = FakeExecutor(trace, sampling_error=sampling_error)
    engine.batch_queue_size = 2
    engine.batch_queue = deque(maxlen=2)
    engine.is_ec_consumer = True
    engine.is_pooling_model = False
    engine.use_spec_decode = False
    engine.async_scheduling = True
    engine.aborts_queue = queue.Queue()
    engine.vllm_config = SimpleNamespace(
        observability_config=SimpleNamespace(
            enable_logging_iteration_details=False
        )
    )
    engine.log_error_detail = lambda output: contextlib.nullcontext()
    engine.log_iteration_details = lambda output: contextlib.nullcontext()
    return engine


def _output(name: str, tokens: int = 1, *, structured: bool = False):
    return SimpleNamespace(
        name=name,
        total_num_scheduled_tokens=tokens,
        pending_structured_output_tokens=["pending"] if structured else [],
    )


def test_engine_core_source_has_no_pair_scheduler_hooks() -> None:
    EngineCore, _ = _load_vllm_classes()
    source = inspect.getsource(EngineCore)
    assert "PAIR_SCHED" not in source
    assert "pair_forward_gate" not in source


@pytest.mark.parametrize("mode", ["off", "elastic"])
def test_real_engine_core_preserves_native_async_queue(mode: str) -> None:
    os.environ["VLLM_PAIR_SCHED_MODE"] = mode
    trace: list[str] = []
    engine = _engine([_output("one"), _output("two")], trace)

    first, executed = engine.step_with_batch_queue()
    assert first is None and executed
    assert len(engine.batch_queue) == 1

    second, executed = engine.step_with_batch_queue()
    assert second == {0: "token-one"} and executed
    assert len(engine.batch_queue) == 1
    assert trace == [
        "schedule:one",
        "execute:one",
        "grammar:one",
        "sample:one",
        "schedule:two",
        "execute:two",
        "grammar:two",
        "sample:two",
        "sample:one.result",
        "update:one:token-one",
    ]


def test_real_engine_core_structured_output_defers_only_native_sampling() -> None:
    trace: list[str] = []
    engine = _engine([_output("one"), _output("two", structured=True)], trace)
    engine.step_with_batch_queue()
    output, _ = engine.step_with_batch_queue()
    assert output == {0: "token-one"}
    assert trace.index("execute:two") < trace.index("sample:one.result")
    assert trace.index("sample:one.result") < trace.index("sample:two")


def test_real_engine_core_empty_plan_bypasses_sampling() -> None:
    trace: list[str] = []
    engine = _engine([_output("empty", tokens=0)], trace)
    output, executed = engine.step_with_batch_queue()
    assert output == {0: "forward-empty"}
    assert not executed
    assert "sample:empty" not in trace


def test_real_engine_core_sampling_exception_follows_native_path() -> None:
    trace: list[str] = []
    engine = _engine([_output("one")], trace, sampling_error=True)
    engine.step_with_batch_queue()
    with pytest.raises(RuntimeError, match="sampling failed"):
        engine.step_with_batch_queue()


def test_real_worker_proc_gates_only_nonempty_execute_model() -> None:
    _, WorkerProc = _load_vllm_classes()
    trace: list[str] = []

    class StopLoop(BaseException):
        pass

    class MQ:
        def __init__(self):
            self.calls = deque(
                [
                    ("execute_model", (_output("one"),), {}, 0),
                    ("sample_tokens", ("grammar",), {}, 0),
                    ("execute_model", (_output("empty", tokens=0),), {}, 0),
                ]
            )

        def dequeue(self, indefinite=True):
            assert indefinite
            if not self.calls:
                raise StopLoop
            return self.calls.popleft()

    class Worker:
        def execute_model(self, output):
            trace.append(f"worker.execute:{output.name}")
            return output.name

        def sample_tokens(self, grammar):
            trace.append(f"worker.sample:{grammar}")
            return grammar

    class Gate:
        def enter_forward(self):
            trace.append("gate.enter")
            return 1, 9

        def leave_forward(self, sequence, grant):
            assert (sequence, grant) == (1, 9)
            trace.append("gate.leave")

        def fail(self, reason):
            trace.append(f"gate.fail:{reason}")

    proc = WorkerProc.__new__(WorkerProc)
    proc.rank = 0
    proc.worker = Worker()
    proc.rpc_broadcast_mq = MQ()
    proc._pair_forward_gate = Gate()
    proc.handle_output = lambda output: trace.append(f"output:{output}")
    with pytest.raises(StopLoop):
        proc.worker_busy_loop()
    assert trace == [
        "gate.enter",
        "worker.execute:one",
        "gate.leave",
        "output:one",
        "worker.sample:grammar",
        "output:grammar",
        "worker.execute:empty",
        "output:empty",
    ]


def test_real_worker_proc_forward_exception_fails_pair() -> None:
    _, WorkerProc = _load_vllm_classes()
    trace: list[str] = []

    class StopLoop(BaseException):
        pass

    class MQ:
        first = True

        def dequeue(self, indefinite=True):
            if self.first:
                self.first = False
                return "execute_model", (_output("bad"),), {}, 0
            raise StopLoop

    class Worker:
        def execute_model(self, output):
            raise RuntimeError("fake forward failed")

    class Gate:
        def enter_forward(self):
            trace.append("gate.enter")
            return 1, 9

        def leave_forward(self, sequence, grant):
            trace.append("gate.leave")

        def fail(self, reason):
            trace.append(f"gate.fail:{reason}")

    proc = WorkerProc.__new__(WorkerProc)
    proc.rank = 0
    proc.worker = Worker()
    proc.rpc_broadcast_mq = MQ()
    proc._pair_forward_gate = Gate()
    proc.handle_output = lambda output: trace.append(
        f"output:{type(output).__name__}"
    )
    with pytest.raises(StopLoop):
        proc.worker_busy_loop()
    assert trace == ["gate.enter", "gate.fail:201", "output:RuntimeError"]
