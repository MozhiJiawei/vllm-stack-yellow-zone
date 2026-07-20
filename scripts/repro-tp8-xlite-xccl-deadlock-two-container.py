#!/usr/bin/env python3
"""Run the TP8 xLite/XCCL deadlock reproducer across two vCANN containers.

Run one coordinator on the host and one participant in each container.  Model
A owns all work in container A; model B owns all work in container B.  The host
coordinator reproduces the phase ordering of the original single-process
script without joining either model's XCCL communicator.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import importlib.util
import multiprocessing as mp
from multiprocessing.connection import Connection, wait
import os
from pathlib import Path
import random
import shlex
import socket
import sys
import time
import traceback
from types import ModuleType
from typing import Any

from two_container_control import (
    ControlError,
    CoordinatorServer,
    command,
    connect_participant,
    event,
)


EXPERIMENT = "repro-tp8-xlite-xccl-deadlock"
TP = 8
MODELS = ("A", "B")
WORKERS = TP * len(MODELS)


def load_legacy() -> ModuleType:
    """Load the proven workload implementation without changing that script."""
    path = Path(__file__).with_name("repro-tp8-xlite-xccl-deadlock.py")
    spec = importlib.util.spec_from_file_location("_xlite_deadlock_legacy", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load legacy workload from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


legacy = load_legacy()


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def free_xlite_port() -> int:
    """Find an xLite base port whose base+400 companion is also available."""
    for _ in range(1000):
        port = random.randint(20000, 39000)
        sockets: list[socket.socket] = []
        try:
            for candidate in (port, port + 400):
                sock = socket.socket()
                sock.bind(("127.0.0.1", candidate))
                sockets.append(sock)
            return port
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("cannot find a free XLITE_PORT pair")


def worker(
    model: str,
    rank: int,
    port: int,
    config: dict[str, Any],
    warmup: Any,
    run_first: Any,
    run_second: Any,
    first_barrier: Any,
    status: Connection,
) -> None:
    """Run one rank, preserving the legacy xLite/NPU call sequence."""
    label = f"model={model} rank={rank} npu={rank}"
    args = argparse.Namespace(**config)
    try:
        os.environ["XLITE_NODE_IPS"] = "127.0.0.1"
        os.environ["XLITE_PORT"] = str(port)
        os.environ["XLITE_DISABLE_XCCL"] = "false"
        os.environ.pop("HCCL_DETERMINISTIC", None)

        import torch
        import torch_npu  # noqa: F401 - registers the NPU backend
        import xlite
        from xlite._C import AttnMHA, AttnMeta, Model, ModelConfig, Runtime

        if model == "A" and rank == 0:
            try:
                version = importlib.metadata.version("xlite")
            except importlib.metadata.PackageNotFoundError:
                version = "unknown"
            log(
                f"RUNTIME torch={torch.__version__} torch_npu={torch_npu.__version__} "
                f"xlite={version} module={xlite.__file__}"
            )

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 0, rank, TP, 1)
        dtype = torch.bfloat16 if args.dtype == "bf16" else torch.float16
        forward_args = legacy.make_model_forward(
            torch,
            rank,
            runtime,
            args.batch,
            args.cached_tokens,
            dtype,
            args.layers,
            args.hidden_size,
            args.intermediate_size,
            Model,
            ModelConfig,
            AttnMeta,
            AttnMHA,
        )
        torch.npu.synchronize()
        status.send(("READY", model, rank, label))

        warmup.wait()
        forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
        torch.npu.synchronize()
        if not bool(torch.isfinite(forward_args[6]).all().cpu().item()):
            raise RuntimeError(
                "warm-up native Model.forward produced non-finite output"
            )
        status.send(("ARMED", model, rank, label))

        if args.schedule == "aligned":
            run_first.wait()
            status.send(("MODEL_ENTER_ALIGNED", model, rank, label))
            log(f"ENTER xLite Model.forward layers={args.layers}: {label}")
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_ALIGNED", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN xLite Model.forward: {label}")
            status.send(("DONE", model, rank, label))
            return

        if legacy.first_wave(model, rank):
            run_first.wait()
            first_barrier.wait()
            status.send(("MODEL_ENTER_FIRST", model, rank, label))
            log(
                f"ENTER first complete xLite Model.forward layers={args.layers}: {label}"
            )
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_FIRST", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN first complete xLite Model.forward: {label}")
        else:
            run_second.wait()
            status.send(("MODEL_ENTER_SECOND", model, rank, label))
            log(
                f"ENTER second complete xLite Model.forward layers={args.layers}: {label}"
            )
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_SECOND", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN second complete xLite Model.forward: {label}")
        status.send(("DONE", model, rank, label))
    except BaseException:
        detail = traceback.format_exc()
        log(f"WORKER ERROR: {label}\n{detail}")
        try:
            status.send(("ERROR", model, rank, detail))
        except BaseException:
            pass
        raise
    finally:
        status.close()


def stop(processes: list[mp.Process]) -> None:
    for process in processes:
        process.join(timeout=0.5)
    for process in processes:
        if process.is_alive():
            process.terminate()
    for process in processes:
        process.join(timeout=5)
    for process in processes:
        if process.is_alive():
            process.kill()
            process.join(timeout=2)


def prepare_participant_environment() -> None:
    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")


def run_participant(args: argparse.Namespace) -> int:
    if sys.platform != "linux":
        log("ERROR: roles A and B must run inside Linux Ascend containers")
        return 2
    if not args.coordinator:
        raise ValueError("--coordinator HOST:PORT is required for roles A and B")
    prepare_participant_environment()
    peer = connect_participant(
        args.coordinator, EXPERIMENT, args.run_id, args.role, args.connect_timeout
    )
    ctx = mp.get_context("spawn")
    processes: list[mp.Process] = []
    parents: list[Connection] = []
    reported_exits: set[int] = set()
    warmup = ctx.Event()
    run_first = ctx.Event()
    run_second = ctx.Event()
    first_barrier = ctx.Barrier(TP // 2)
    initialized = False
    try:
        while True:
            if initialized and parents:
                for connection in wait(parents, timeout=0):
                    try:
                        kind, model, rank, detail = connection.recv()
                    except EOFError:
                        parents.remove(connection)
                        connection.close()
                        continue
                    peer.send(event(kind, model=model, rank=rank, detail=str(detail)))
            for index, process in enumerate(processes):
                if process.exitcode not in (None, 0) and index not in reported_exits:
                    reported_exits.add(index)
                    peer.send(
                        event(
                            "ERROR",
                            model=args.role,
                            rank=index,
                            detail=f"worker exited with code {process.exitcode}",
                        )
                    )

            try:
                message = peer.recv(timeout=0.02)
            except TimeoutError:
                continue
            if message.get("type") == "FINISH":
                exit_code = int(message.get("exit_code", 2))
                result = str(message.get("result", "UNKNOWN"))
                log(f"RESULT={result} exit_code={exit_code} source=coordinator")
                stop(processes)
                processes.clear()
                try:
                    peer.send({"type": "FINISH_ACK"})
                except ControlError as error:
                    log(f"FINISH ACK failed after cleanup: {error}")
                return exit_code
            if message.get("type") != "COMMAND":
                raise ControlError(f"unexpected coordinator message: {message}")
            name = message.get("name")
            if name == "INIT":
                if initialized:
                    raise ControlError("received duplicate INIT")
                config = message.get("config")
                if not isinstance(config, dict):
                    raise ControlError("INIT config must be an object")
                port = args.xlite_port or free_xlite_port()
                log(
                    f"INIT role={args.role} tp={TP} xlite_port={port} "
                    f"schedule={config.get('schedule')}"
                )
                for rank in range(TP):
                    parent, child = ctx.Pipe(duplex=False)
                    process = ctx.Process(
                        target=worker,
                        name=f"model-{args.role}-rank-{rank}",
                        args=(
                            args.role,
                            rank,
                            port,
                            config,
                            warmup,
                            run_first,
                            run_second,
                            first_barrier,
                            child,
                        ),
                    )
                    process.start()
                    child.close()
                    processes.append(process)
                    parents.append(parent)
                initialized = True
            elif name == "WARMUP":
                if not initialized:
                    raise ControlError("received WARMUP before INIT")
                warmup.set()
            elif name in ("RUN_ALIGNED", "RUN_FIRST"):
                if not initialized:
                    raise ControlError(f"received {name} before INIT")
                run_first.set()
            elif name == "RUN_SECOND":
                if not initialized:
                    raise ControlError("received RUN_SECOND before INIT")
                run_second.set()
            else:
                raise ControlError(f"unknown command: {name}")
    except (ControlError, OSError, ValueError):
        log(f"CONTROL ERROR:\n{traceback.format_exc()}")
        return 2
    finally:
        try:
            first_barrier.abort()
        except BaseException:
            pass
        stop(processes)
        for connection in parents:
            connection.close()
        peer.close()
        log("CLEANUP complete")


class OperationStates:
    def __init__(self) -> None:
        self.messages: list[tuple[str, str, int, str]] = []
        self.states: dict[tuple[str, int], str] = {}

    def add(self, role: str, message: dict[str, Any]) -> None:
        if message.get("type") != "EVENT":
            raise ControlError(f"unexpected message from role {role}: {message}")
        kind = message.get("kind")
        model = message.get("model")
        rank = message.get("rank")
        detail = message.get("detail", "")
        if not isinstance(kind, str) or model != role or not isinstance(rank, int):
            raise ControlError(f"invalid worker event from role {role}: {message}")
        self.messages.append((kind, model, rank, str(detail)))
        self.states[(model, rank)] = kind
        if kind == "ERROR":
            raise ControlError(f"worker failed: model={model} rank={rank}\n{detail}")

    def count(self, kind: str) -> int:
        return sum(item[0] == kind for item in self.messages)

    def count_model(self, kind: str, model: str) -> int:
        return sum(item[0] == kind and item[1] == model for item in self.messages)

    def print(self) -> None:
        log("OPERATION STATE:")
        for model in MODELS:
            for rank in range(TP):
                state = self.states.get((model, rank), "NO_STATUS")
                log(f"  model={model} rank={rank} npu={rank} state={state}")


def collect_until(
    server: CoordinatorServer,
    states: OperationStates,
    kind: str,
    count: int,
    timeout: float,
    model: str | None = None,
) -> bool:
    deadline = time.monotonic() + timeout

    def current() -> int:
        return (
            states.count_model(kind, model) if model is not None else states.count(kind)
        )

    while current() < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        try:
            role, message = server.recv_any(remaining)
        except TimeoutError:
            return False
        states.add(role, message)
    return True


def workload_config(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "schedule": args.schedule,
        "batch": args.batch,
        "hidden_size": args.hidden_size,
        "intermediate_size": args.intermediate_size,
        "layers": args.layers,
        "cached_tokens": args.cached_tokens,
        "dtype": args.dtype,
    }


def validate_workload(args: argparse.Namespace) -> None:
    if args.batch <= 0 or args.layers <= 0 or args.intermediate_size <= 0:
        raise ValueError("batch, layers and intermediate-size must be positive")
    if args.hidden_size != 5120:
        raise ValueError("this Qwen3-32B reproducer requires --hidden-size 5120")
    if args.intermediate_size % TP:
        raise ValueError("intermediate-size must be divisible by TP=8")
    if args.cached_tokens <= legacy.FLASH_TILE:
        raise ValueError("cached-tokens must exceed 8192 to select FlashAttention")


def run_coordinator(args: argparse.Namespace) -> int:
    validate_workload(args)
    states = OperationStates()
    result = "SETUP_FAILED"
    exit_code = 2
    stage = "setup"
    server = CoordinatorServer(
        EXPERIMENT, args.run_id, args.listen_host, args.control_port
    )
    try:
        server.open()
        log(
            f"LISTEN experiment={EXPERIMENT} run_id={args.run_id} "
            f"address={args.listen_host}:{args.control_port}"
        )
        server.accept_roles(MODELS, args.startup_timeout)
        log("connected roles A and B; initializing model A TP8 before model B")
        config = workload_config(args)
        server.send("A", command("INIT", config=config))
        if not collect_until(server, states, "READY", TP, args.startup_timeout, "A"):
            raise ControlError("timed out initializing model A")
        time.sleep(args.init_stagger_seconds)
        server.send("B", command("INIT", config=config))
        if not collect_until(server, states, "READY", TP, args.startup_timeout, "B"):
            raise ControlError("timed out initializing model B")

        server.send("A", command("WARMUP"))
        if not collect_until(server, states, "ARMED", TP, args.startup_timeout, "A"):
            raise ControlError("timed out warming model A")
        server.send("B", command("WARMUP"))
        if not collect_until(server, states, "ARMED", TP, args.startup_timeout, "B"):
            raise ControlError("timed out warming model B")
        stage = "runtime"

        log(f"COMMAND: {shlex.join(sys.argv)}")
        log(
            f"CONFIG: physical_910B4=true tp=8 schedule={args.schedule} "
            f"native_model_forward=true layers={args.layers} "
            f"decode_batch={args.batch} hidden_size={args.hidden_size} "
            f"intermediate_size={args.intermediate_size} dtype={args.dtype} "
            f"cached_tokens={args.cached_tokens} two_container=true"
        )
        if args.schedule == "aligned":
            log("CONTROL: releasing two complete TP8 groups")
            server.broadcast(command("RUN_ALIGNED"))
            complete = collect_until(server, states, "DONE", WORKERS, args.hang_timeout)
            if not complete:
                log(f"CONTROL: confirming apparent hang for {args.confirm_timeout}s")
                complete = collect_until(
                    server, states, "DONE", WORKERS, args.confirm_timeout
                )
            states.print()
            if complete:
                result, exit_code = "NORMAL_CONTROL_PASSED", 0
            else:
                result, exit_code = "NORMAL_CONTROL_HUNG", 1
        else:
            log("CROSSED: releasing 8 first complete native Model.forward calls")
            server.broadcast(command("RUN_FIRST"))
            if not collect_until(
                server, states, "MODEL_SUBMITTED_FIRST", TP, args.hang_timeout
            ):
                states.print()
                result, exit_code = "FIRST_WAVE_FAILED", 2
            else:
                time.sleep(args.stagger_seconds)
                log("CROSSED: releasing 8 opposite-model native Model.forward calls")
                server.broadcast(command("RUN_SECOND"))
                complete = collect_until(
                    server, states, "DONE", WORKERS, args.hang_timeout
                )
                if not complete:
                    log(f"CROSSED: confirming stable hang for {args.confirm_timeout}s")
                    complete = collect_until(
                        server, states, "DONE", WORKERS, args.confirm_timeout
                    )
                states.print()
                if complete:
                    result, exit_code = "NO_DEADLOCK", 1
                else:
                    entered = states.count("MODEL_ENTER_SECOND")
                    first_submitted = states.count("MODEL_SUBMITTED_FIRST")
                    second_submitted = states.count("MODEL_SUBMITTED_SECOND")
                    model_done = states.count("MODEL_DONE")
                    log(
                        f"HANG: done={states.count('DONE')}/16 "
                        f"first_submitted={first_submitted}/8 "
                        f"second_enter={entered}/8 "
                        f"second_submitted={second_submitted}/8 "
                        f"model_done={model_done}/16"
                    )
                    if first_submitted == TP and entered == TP and model_done == 0:
                        result, exit_code = "DEADLOCK_REPRODUCED", 0
                    else:
                        result, exit_code = "HUNG_AFTER_PROGRESS_INCONCLUSIVE", 2
    except (ControlError, OSError, ValueError):
        log(f"COORDINATOR ERROR:\n{traceback.format_exc()}")
        result = "RUNTIME_FAILED" if stage == "runtime" else "SETUP_FAILED"
        exit_code = 2
    finally:
        log(f"RESULT={result} exit_code={exit_code}")
        try:
            server.finish(result, exit_code)
        except ControlError:
            log(f"FINISH ERROR:\n{traceback.format_exc()}")
        server.close()
    return exit_code


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--role", choices=("coordinator", "A", "B"), required=True)
    result.add_argument("--run-id", required=True, help="unique ID shared by all roles")
    result.add_argument("--listen-host", default="0.0.0.0")
    result.add_argument("--control-port", type=int, default=29680)
    result.add_argument("--coordinator", help="coordinator endpoint as HOST:PORT")
    result.add_argument("--connect-timeout", type=float, default=600)
    result.add_argument(
        "--xlite-port", type=int, help="participant-local xLite base port"
    )
    result.add_argument("--schedule", choices=("crossed", "aligned"), default="crossed")
    result.add_argument("--batch", type=int, default=16)
    result.add_argument("--hidden-size", type=int, default=5120)
    result.add_argument(
        "--intermediate-size", type=int, default=legacy.QWEN3_INTERMEDIATE
    )
    result.add_argument("--layers", type=int, default=64)
    result.add_argument("--cached-tokens", type=int, default=9216)
    result.add_argument("--dtype", choices=("bf16", "fp16"), default="bf16")
    result.add_argument("--init-stagger-seconds", type=float, default=10)
    result.add_argument("--stagger-seconds", type=float, default=5)
    result.add_argument("--hang-timeout", type=float, default=30)
    result.add_argument("--confirm-timeout", type=float, default=120)
    result.add_argument("--startup-timeout", type=float, default=600)
    return result


def main() -> int:
    args = parser().parse_args()
    if not 1 <= args.control_port <= 65535:
        parser().error("control-port must be between 1 and 65535")
    if not args.run_id.strip():
        parser().error("run-id must not be empty")
    if args.xlite_port is not None and not 1 <= args.xlite_port <= 65135:
        parser().error("xlite-port must be between 1 and 65135")
    timeout_names = (
        "connect_timeout",
        "init_stagger_seconds",
        "stagger_seconds",
        "hang_timeout",
        "confirm_timeout",
        "startup_timeout",
    )
    if any(getattr(args, name) < 0 for name in timeout_names):
        parser().error("timeouts and stagger durations must be non-negative")
    try:
        if args.role == "coordinator":
            return run_coordinator(args)
        return run_participant(args)
    except ControlError as error:
        log(f"CONTROL ERROR: {error}")
        return 2
    except ValueError as error:
        parser().error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
