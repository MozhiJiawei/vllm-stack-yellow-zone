#!/usr/bin/env python3
"""Probe XCCL hold-vs-vector behavior across two vCANN containers.

Container A runs the eight-rank XCCL workload.  Container B runs the Vector
Sigmoid workload.  A host-side coordinator preserves the ordering and decides
the result without importing any Ascend packages.
"""

from __future__ import annotations

import argparse
import multiprocessing as mp
import os
import queue
import random
import socket
import sys
import time
import traceback
from typing import Any

from two_container_control import (
    ControlError,
    CoordinatorServer,
    command,
    connect_participant,
    event,
)


EXPERIMENT = "probe-xccl-hold-vs-vector"
TP = 8
ROLES = ("A", "B")


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def free_xlite_port() -> int:
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


def model_a(rank: int, port: int, start: Any, returned: Any, messages: Any) -> None:
    """Original XCCL workload with status events made externally visible."""
    try:
        os.environ.update(
            XLITE_NODE_IPS="127.0.0.1",
            XLITE_PORT=str(port),
            XLITE_DISABLE_XCCL="false",
        )
        import torch
        import torch_npu  # noqa: F401
        from xlite._C import Runtime, all_reduce

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 128, rank, TP, 1)
        src = torch.ones((16, 5120), dtype=torch.bfloat16, device=f"npu:{rank}")
        dst = torch.empty_like(src)
        torch.npu.synchronize()
        messages.put(("READY", "A", rank, ""))
        start.wait()
        if rank < TP // 2:
            messages.put(("A_CALL", "A", rank, ""))
            all_reduce(runtime, dst, src)
            torch.npu.synchronize()
            returned.set()
            messages.put(("A_RETURN", "A", rank, ""))
        else:
            time.sleep(3600)  # Deliberately withhold half of model A's ranks.
    except BaseException:
        messages.put(("ERROR", "A", rank, traceback.format_exc()))
        raise


def model_b(start: Any, messages: Any) -> None:
    """Original independent Vector Sigmoid workload."""
    try:
        import torch
        import torch_npu  # noqa: F401

        torch.npu.set_device(0)
        x = torch.randn((64 * 524_288,), dtype=torch.float16, device="npu:0")
        torch.npu.synchronize()
        messages.put(("READY", "B", 0, ""))
        start.wait()
        _out = torch.sigmoid(x)
        torch.npu.synchronize()
        messages.put(("B_DONE", "B", 0, ""))
    except BaseException:
        messages.put(("ERROR", "B", 0, traceback.format_exc()))
        raise


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
    messages = ctx.Queue()
    start = ctx.Event()
    returned = ctx.Event()
    processes: list[mp.Process] = []
    reported_exits: set[int] = set()
    initialized = False
    try:
        while True:
            if initialized:
                while True:
                    try:
                        kind, model, rank, detail = messages.get_nowait()
                    except queue.Empty:
                        break
                    peer.send(event(kind, model=model, rank=rank, detail=str(detail)))
            for index, process in enumerate(processes):
                if process.exitcode not in (None, 0) and index not in reported_exits:
                    reported_exits.add(index)
                    rank = index if args.role == "A" else 0
                    peer.send(
                        event(
                            "ERROR",
                            model=args.role,
                            rank=rank,
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
                if args.role == "A":
                    port = args.xlite_port or free_xlite_port()
                    log(f"INIT role=A tp={TP} xlite_port={port}")
                    processes = [
                        ctx.Process(
                            target=model_a,
                            name=f"model-A-rank-{rank}",
                            args=(rank, port, start, returned, messages),
                        )
                        for rank in range(TP)
                    ]
                else:
                    log("INIT role=B vector_worker=1")
                    processes = [
                        ctx.Process(
                            target=model_b,
                            name="model-B-vector",
                            args=(start, messages),
                        )
                    ]
                for process in processes:
                    process.start()
                initialized = True
            elif name in ("START_A", "START_B"):
                expected = f"START_{args.role}"
                if name != expected or not initialized:
                    raise ControlError(f"invalid command for role {args.role}: {name}")
                start.set()
            elif name == "CHECK_A_BLOCKED":
                if args.role != "A" or not initialized:
                    raise ControlError(f"invalid command for role {args.role}: {name}")
                kind = "A_RETURNED" if returned.is_set() else "A_STILL_BLOCKED"
                peer.send(event(kind, model="A", rank=0))
            else:
                raise ControlError(f"unknown command: {name}")
    except (ControlError, OSError, ValueError):
        log(f"CONTROL ERROR:\n{traceback.format_exc()}")
        return 2
    finally:
        stop(processes)
        peer.close()
        log("CLEANUP complete")


class ProbeStates:
    def __init__(self) -> None:
        self.messages: list[tuple[str, str, int]] = []

    def add(self, role: str, message: dict[str, Any]) -> None:
        if message.get("type") != "EVENT":
            raise ControlError(f"unexpected message from role {role}: {message}")
        kind = message.get("kind")
        model = message.get("model")
        rank = message.get("rank")
        if not isinstance(kind, str) or model != role or not isinstance(rank, int):
            raise ControlError(f"invalid worker event from role {role}: {message}")
        if kind == "ERROR":
            raise ControlError(
                f"worker failed: model={model} rank={rank}\n{message.get('detail', '')}"
            )
        self.messages.append((kind, model, rank))

    def count(self, kind: str) -> int:
        return sum(item[0] == kind for item in self.messages)

    def count_role(self, kind: str, role: str) -> int:
        return sum(item[0] == kind and item[1] == role for item in self.messages)


def collect_until(
    server: CoordinatorServer,
    states: ProbeStates,
    kind: str,
    count: int,
    timeout: float,
    role: str | None = None,
) -> bool:
    deadline = time.monotonic() + timeout

    def current() -> int:
        return states.count_role(kind, role) if role is not None else states.count(kind)

    while current() < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        try:
            source, message = server.recv_any(remaining)
        except TimeoutError:
            return False
        states.add(source, message)
    return True


def collect_for(
    server: CoordinatorServer, states: ProbeStates, duration: float
) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        try:
            role, message = server.recv_any(deadline - time.monotonic())
        except TimeoutError:
            return
        states.add(role, message)


def a_is_still_blocked(
    server: CoordinatorServer, states: ProbeStates, timeout: float = 5
) -> bool:
    """Ask A's live parent process, avoiding status-message delivery races."""
    server.send("A", command("CHECK_A_BLOCKED"))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            role, message = server.recv_any(deadline - time.monotonic())
        except TimeoutError:
            raise ControlError("timed out checking model A block state") from None
        states.add(role, message)
        kind = message.get("kind")
        if role == "A" and kind in ("A_STILL_BLOCKED", "A_RETURNED"):
            return kind == "A_STILL_BLOCKED"
    raise ControlError("timed out checking model A block state")


def run_coordinator(args: argparse.Namespace) -> int:
    states = ProbeStates()
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
        server.accept_roles(ROLES, args.startup_timeout)
        server.broadcast(command("INIT"))
        if not collect_until(server, states, "READY", TP, args.startup_timeout, "A"):
            raise ControlError("timed out initializing model A")
        if not collect_until(server, states, "READY", 1, args.startup_timeout, "B"):
            raise ControlError("timed out initializing model B")

        stage = "runtime"
        server.send("A", command("START_A"))
        if not collect_until(server, states, "A_CALL", TP // 2, args.hang_timeout, "A"):
            result, exit_code = "SETUP_FAILED", 2
        else:
            collect_for(server, states, args.hold_confirm_seconds)
            if states.count("A_RETURN") or not a_is_still_blocked(server, states):
                result, exit_code = "ALLREDUCE_DID_NOT_BLOCK", 2
            else:
                log("A is waiting in Vector AllReduce; starting B's Vector Sigmoid")
                server.send("B", command("START_B"))
                passed = collect_until(
                    server, states, "B_DONE", 1, args.vector_timeout, "B"
                )
                if not a_is_still_blocked(server, states):
                    result, exit_code = "ALLREDUCE_DID_NOT_STAY_BLOCKED", 2
                elif passed:
                    result, exit_code = "PASS", 0
                else:
                    result, exit_code = "BLOCKED", 1
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
    result.add_argument("--control-port", type=int, default=29681)
    result.add_argument("--coordinator", help="coordinator endpoint as HOST:PORT")
    result.add_argument("--connect-timeout", type=float, default=600)
    result.add_argument("--xlite-port", type=int, help="participant A xLite base port")
    result.add_argument("--startup-timeout", type=float, default=300)
    result.add_argument("--hang-timeout", type=float, default=30)
    result.add_argument("--hold-confirm-seconds", type=float, default=3)
    result.add_argument("--vector-timeout", type=float, default=30)
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
        "startup_timeout",
        "hang_timeout",
        "hold_confirm_seconds",
        "vector_timeout",
    )
    if any(getattr(args, name) < 0 for name in timeout_names):
        parser().error("timeouts and hold duration must be non-negative")
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
