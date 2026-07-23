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
    from vllm.v1.executor.uniproc_executor import UniProcExecutor

    return EngineCore, WorkerProc, UniProcExecutor


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
    EngineCore, _, _ = _load_vllm_classes()
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
    EngineCore, _, _ = _load_vllm_classes()
    source = inspect.getsource(EngineCore)
    assert "PAIR_SCHED" not in source
    assert "pair_forward_gate" not in source


def test_mode_off_worker_hot_paths_remain_native() -> None:
    _, WorkerProc, UniProcExecutor = _load_vllm_classes()
    worker_loop = inspect.getsource(WorkerProc.worker_busy_loop)
    uniproc_execute = inspect.getsource(UniProcExecutor.execute_model)

    assert "pair_forward_gate" not in worker_loop
    assert "PAIR_SCHED" not in worker_loop
    assert "pair_forward_gate" not in uniproc_execute
    assert "PAIR_SCHED" not in uniproc_execute


def test_worker_gate_is_installed_once_and_wraps_only_forward(monkeypatch) -> None:
    _load_vllm_classes()
    from vllm.v1.executor import multiproc_executor

    trace: list[str] = []

    class Gate:
        def enter_forward(self):
            trace.append("gate.enter")
            return 1, 9

        def leave_forward(self, sequence, grant):
            trace.append(f"gate.leave:{sequence}:{grant}")

        def fail(self, reason):
            trace.append(f"gate.fail:{reason}")

    gate = Gate()
    calls = []
    module = SimpleNamespace(
        create_worker_forward_gate_from_env=lambda **kwargs: (
            calls.append(kwargs) or gate
        )
    )
    monkeypatch.setitem(sys.modules, "vllm_pair_scheduler", module)
    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "elastic")
    config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            local_world_size=4,
        )
    )

    def execute_model(output):
        trace.append(f"worker.execute:{output.name}")
        return output.name

    worker = SimpleNamespace(execute_model=execute_model)
    assert (
        multiproc_executor._install_pair_worker_gate(worker, config, rank=2)
        is gate
    )
    assert calls == [{"worker_rank": 2, "worker_count": 4}]

    assert worker.execute_model(_output("gated")) == "gated"
    assert worker.execute_model(_output("empty", tokens=0)) == "empty"

    assert trace == [
        "gate.enter",
        "worker.execute:gated",
        "gate.leave:1:9",
        "worker.execute:empty",
    ]


def test_uniproc_gate_is_installed_at_worker_boundary(monkeypatch) -> None:
    _load_vllm_classes()
    from vllm.v1.executor import uniproc_executor

    trace: list[str] = []

    class Gate:
        def enter_forward(self):
            trace.append("gate.enter")
            return 1, 9

        def leave_forward(self, sequence, grant):
            trace.append(f"gate.leave:{sequence}:{grant}")

        def fail(self, reason):
            trace.append(f"gate.fail:{reason}")

    gate = Gate()
    calls = []
    module = SimpleNamespace(
        create_worker_forward_gate_from_env=lambda **kwargs: (
            calls.append(kwargs) or gate
        )
    )
    monkeypatch.setitem(sys.modules, "vllm_pair_scheduler", module)
    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "elastic")
    config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            world_size=1,
        )
    )

    def execute_model(output):
        trace.append(f"worker.execute:{output.name}")
        return output.name

    worker = SimpleNamespace(execute_model=execute_model)
    assert uniproc_executor._install_pair_worker_gate(worker, config) is gate
    assert calls == [{"worker_rank": 0, "worker_count": 1}]
    assert worker.execute_model(_output("gated")) == "gated"
    assert trace == [
        "gate.enter",
        "worker.execute:gated",
        "gate.leave:1:9",
    ]


def test_mode_off_does_not_replace_worker_execute_model(monkeypatch) -> None:
    _load_vllm_classes()
    from vllm.v1.executor import multiproc_executor
    from vllm.v1.executor import uniproc_executor

    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "off")
    worker = SimpleNamespace(execute_model=lambda output: output)
    original_execute_model = worker.execute_model
    uniproc_config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            world_size=1,
        )
    )
    multiproc_config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            local_world_size=4,
        )
    )

    assert (
        uniproc_executor._install_pair_worker_gate(worker, uniproc_config)
        is None
    )
    assert worker.execute_model is original_execute_model
    assert (
        multiproc_executor._install_pair_worker_gate(
            worker, multiproc_config, rank=0
        )
        is None
    )
    assert worker.execute_model is original_execute_model


def test_installed_worker_gate_fails_pair_on_forward_exception(monkeypatch) -> None:
    _load_vllm_classes()
    from vllm.v1.executor import uniproc_executor

    trace: list[str] = []

    class Gate:
        def enter_forward(self):
            trace.append("gate.enter")
            return 2, 10

        def leave_forward(self, sequence, grant):
            trace.append("gate.leave")

        def fail(self, reason):
            trace.append(f"gate.fail:{reason}")

    gate = Gate()
    monkeypatch.setitem(
        sys.modules,
        "vllm_pair_scheduler",
        SimpleNamespace(
            create_worker_forward_gate_from_env=lambda **kwargs: gate
        ),
    )
    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "elastic")
    config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            world_size=1,
        )
    )
    worker = SimpleNamespace(
        execute_model=lambda output: (_ for _ in ()).throw(
            RuntimeError("fake forward failed")
        )
    )
    uniproc_executor._install_pair_worker_gate(worker, config)

    with pytest.raises(RuntimeError, match="fake forward failed"):
        worker.execute_model(_output("bad"))
    assert trace == ["gate.enter", "gate.fail:201"]


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


def test_real_worker_proc_gates_only_nonempty_execute_model(monkeypatch) -> None:
    _, WorkerProc, _ = _load_vllm_classes()
    from vllm.v1.executor import multiproc_executor

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
    gate = Gate()
    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "elastic")
    monkeypatch.setitem(
        sys.modules,
        "vllm_pair_scheduler",
        SimpleNamespace(
            create_worker_forward_gate_from_env=lambda **kwargs: gate
        ),
    )
    config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            local_world_size=1,
        )
    )
    proc._pair_forward_gate = multiproc_executor._install_pair_worker_gate(
        proc.worker, config, rank=0
    )
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


def test_real_worker_proc_forward_exception_fails_pair(monkeypatch) -> None:
    _, WorkerProc, _ = _load_vllm_classes()
    from vllm.v1.executor import multiproc_executor

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
    gate = Gate()
    monkeypatch.setenv("VLLM_PAIR_SCHED_MODE", "elastic")
    monkeypatch.setitem(
        sys.modules,
        "vllm_pair_scheduler",
        SimpleNamespace(
            create_worker_forward_gate_from_env=lambda **kwargs: gate
        ),
    )
    config = SimpleNamespace(
        parallel_config=SimpleNamespace(
            pipeline_parallel_size=1,
            nnodes_within_dp=1,
            local_world_size=1,
        )
    )
    proc._pair_forward_gate = multiproc_executor._install_pair_worker_gate(
        proc.worker, config, rank=0
    )
    proc.handle_output = lambda output: trace.append(
        f"output:{type(output).__name__}"
    )
    with pytest.raises(StopLoop):
        proc.worker_busy_loop()
    assert trace == ["gate.enter", "gate.fail:201", "output:RuntimeError"]
