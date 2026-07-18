#!/usr/bin/env python3
"""Reproduce a possible two-model TP=8 HCCL scheduling deadlock.

The only non-stdlib runtime dependencies are torch and torch_npu.  Run this
inside a Linux Ascend container exposing exactly (or at least) NPUs 0..7:

    python3 scripts/repro-tp8-hccl-deadlock.py

Two independent 8-rank HCCL worlds model two TP=8 engines.  Model A rank N
and model B rank N share NPU N.  After sequential communicator warm-ups, the
script creates this order:

    NPU 0..3: A all-reduce first, B all-reduce second
    NPU 4..7: B all-reduce first, A all-reduce second

Each rank captures a minimal full-decode ACL graph. Every synthetic layer has
an in-place FP16 Vector Core operation followed by a TP=8 all-reduce, matching
vLLM's FULL_DECODE_ONLY property that the entire multi-layer decode is replayed
as one graph. Thus the first wave contains four graph replays from each model.
If an incomplete AIV HCCL operation monopolizes device execution resources,
the opposite model's second-wave graph cannot pass its compute operator to
submit the missing all-reduce rank. This creates the intended hold-and-wait
cycle without model parameters.

Exit status is 0 when a hang is observed (hypothesis reproduced), 1 when all
collectives complete, and 2 when setup or launch fails.
"""

from __future__ import annotations

import argparse
import faulthandler
import multiprocessing as mp
import os
import queue
import signal
import shlex
import socket
import sys
import time
import traceback
from datetime import timedelta
from typing import Any


TP_SIZE = 8
MODEL_NAMES = ("A", "B")
FIRST_WAVE_SIZE = TP_SIZE


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def is_first_wave(model: str, rank: int) -> bool:
    return (model == "A" and rank < TP_SIZE // 2) or (
        model == "B" and rank >= TP_SIZE // 2
    )


def close_worker_queue(status_queue: Any) -> None:
    try:
        status_queue.close()
        status_queue.join_thread()
    except BaseException as error:
        log(f"WARNING: failed to flush worker status queue: {error}")


def worker(
    model: str,
    rank: int,
    master_port: int,
    tensor_kib: int,
    compute_mib: int,
    decode_layers: int,
    execution_mode: str,
    phase_barrier: Any,
    race_start: Any,
    second_wave_start: Any,
    status_queue: Any,
    hccl_timeout: int,
) -> None:
    # Import in spawned children so the parent does not initialize the NPU.
    import torch
    import torch.distributed as dist
    import torch_npu  # noqa: F401 - registers the NPU device and HCCL backend

    device = rank
    label = f"model={model} rank={rank} npu={device}"
    wave = "first" if is_first_wave(model, rank) else "second"
    faulthandler.enable(file=sys.stderr, all_threads=True)
    faulthandler.register(signal.SIGUSR1, file=sys.stderr, all_threads=True)
    try:
        log(f"INIT begin: {label} pid={os.getpid()}")
        if model == "A" and rank == 0:
            log(
                "RUNTIME "
                f"torch={torch.__version__} torch_npu={torch_npu.__version__} "
                f"visible_npus={torch.npu.device_count()}"
            )
        torch.npu.set_device(device)
        log(f"DEVICE selected: {label}")
        dist.init_process_group(
            backend="hccl",
            init_method=f"tcp://127.0.0.1:{master_port}",
            rank=rank,
            world_size=TP_SIZE,
            timeout=timedelta(seconds=hccl_timeout),
        )
        log(f"HCCL group ready: {label}")

        # A decode TP all-reduce is typically only tens to hundreds of KiB
        # (decode tokens x hidden size) and uses a 16-bit model dtype.
        tensor = torch.full(
            (tensor_kib * 512,),
            float(rank + 1),
            dtype=torch.float16,
            device=f"npu:{device}",
        )
        compute_input = None
        if execution_mode == "aclgraph" or wave == "second":
            compute_input = torch.randn(
                (compute_mib * 524_288,),
                dtype=torch.float16,
                device=f"npu:{device}",
            )
            torch.npu.synchronize()

        # Finish group creation in both models before warm-up.
        phase_barrier.wait()

        # Warm up one model at a time. This makes communicator construction a
        # setup concern rather than part of the deadlock being tested.
        if model == "A":
            log(f"WARMUP begin: {label}")
            dist.all_reduce(tensor)
            torch.npu.synchronize()
            log(f"WARMUP done: {label}")
        phase_barrier.wait()

        tensor.fill_(float(rank + 1))
        if model == "B":
            log(f"WARMUP begin: {label}")
            dist.all_reduce(tensor)
            torch.npu.synchronize()
            log(f"WARMUP done: {label}")
        phase_barrier.wait()

        tensor.fill_(float(rank + 1))
        torch.npu.synchronize()

        aclgraph = None
        graph_output = None
        if execution_mode == "aclgraph":
            # vLLM-Ascend captures model computation and AIV-expanded HCCL in
            # an NPUGraph. Capture one model at a time with all eight ranks so
            # capture itself completes before the crossed replay begins.
            aclgraph = torch.npu.NPUGraph()
            if model == "A":
                log(f"ACL GRAPH capture begin: {label}")
                assert compute_input is not None
                with torch.npu.graph(aclgraph):
                    for _ in range(decode_layers):
                        compute_input.sigmoid_()
                        dist.all_reduce(tensor)
                        tensor.mul_(1.0 / TP_SIZE)
                    graph_output = compute_input
                log(f"ACL GRAPH capture done: {label}")
            phase_barrier.wait()

            if model == "B":
                log(f"ACL GRAPH capture begin: {label}")
                assert compute_input is not None
                with torch.npu.graph(aclgraph):
                    for _ in range(decode_layers):
                        compute_input.sigmoid_()
                        dist.all_reduce(tensor)
                        tensor.mul_(1.0 / TP_SIZE)
                    graph_output = compute_input
                log(f"ACL GRAPH capture done: {label}")
            phase_barrier.wait()

            assert graph_output is not None
            tensor.fill_(float(rank + 1))
            torch.npu.synchronize()

        status_queue.put(("ARMED", model, rank, device, ""))
        log(f"ARMED: {label}")
        race_start.wait()

        if execution_mode == "aclgraph":
            if wave == "second":
                second_wave_start.wait()
            status_queue.put(("OP_ENTERED", model, rank, device, wave))
            log(f"ENTER ACL graph replay ({wave} wave): {label}")
            assert aclgraph is not None
            aclgraph.replay()
            log(f"ACL graph replay returned; synchronizing ({wave} wave): {label}")
            torch.npu.synchronize()
            log(f"DONE ACL graph replay ({wave} wave): {label}")
        else:
            if wave == "second":
                second_wave_start.wait()
                log(f"ENTER Vector Core compute before all_reduce: {label}")
                status_queue.put(("COMPUTE_STARTED", model, rank, device, wave))
                assert compute_input is not None
                compute_output = torch.sigmoid(compute_input)
                torch.npu.synchronize()
                del compute_output
                status_queue.put(("COMPUTE_DONE", model, rank, device, wave))
                log(f"DONE Vector Core compute before all_reduce: {label}")

            status_queue.put(("OP_ENTERED", model, rank, device, wave))
            log(f"ENTER synchronous all_reduce ({wave} wave): {label}")
            dist.all_reduce(tensor)
            torch.npu.synchronize()
            log(f"RETURN synchronous all_reduce ({wave} wave): {label}")

        value = float(tensor[0].cpu().item())
        expected = float(
            (TP_SIZE * (TP_SIZE + 1) // 2) / TP_SIZE
            if execution_mode == "aclgraph"
            else TP_SIZE * (TP_SIZE + 1) // 2
        )
        if value != expected:
            raise RuntimeError(
                f"wrong all-reduce result: got {value}, expected {expected}"
            )

        dist.destroy_process_group()
        status_queue.put(("DONE", model, rank, device, wave))
        close_worker_queue(status_queue)
        log(f"DONE all_reduce ({wave} wave): {label}")
    except BaseException:
        error = traceback.format_exc()
        log(f"WORKER ERROR: {label}\n{error}")
        status_queue.put(("ERROR", model, rank, device, error))
        close_worker_queue(status_queue)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--decode-layers",
        type=int,
        default=32,
        help="synthetic compute+all-reduce layers in the full decode graph (default: 32)",
    )
    parser.add_argument(
        "--execution-mode",
        choices=("aclgraph", "eager"),
        default="aclgraph",
        help="run a captured compute+all-reduce graph or the eager control (default: aclgraph)",
    )
    parser.add_argument(
        "--tensor-kib",
        type=int,
        default=256,
        help="FP16 decode all-reduce tensor size per rank in KiB (default: 256)",
    )
    parser.add_argument(
        "--compute-mib",
        type=int,
        default=64,
        help="FP16 Vector Core input size per second-wave rank in MiB (default: 64)",
    )
    parser.add_argument(
        "--stagger-seconds",
        type=float,
        default=2.0,
        help="delay after all first-wave ranks enter replay/all_reduce before releasing the second wave",
    )
    parser.add_argument(
        "--hang-timeout",
        type=float,
        default=30.0,
        help="seconds to wait for all ranks after releasing the second wave",
    )
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=300.0,
        help="seconds allowed for HCCL initialization and sequential warm-ups",
    )
    parser.add_argument(
        "--enter-timeout",
        type=float,
        default=60.0,
        help="seconds allowed for all first-wave ranks to enter replay/all_reduce",
    )
    parser.add_argument(
        "--hccl-timeout",
        type=int,
        default=1800,
        help="torch.distributed HCCL timeout; keep above hang-timeout",
    )
    args = parser.parse_args()
    if args.tensor_kib <= 0:
        parser.error("--tensor-kib must be positive")
    if args.compute_mib <= 0:
        parser.error("--compute-mib must be positive")
    if args.decode_layers <= 0:
        parser.error("--decode-layers must be positive")
    if (
        min(
            args.stagger_seconds,
            args.hang_timeout,
            args.startup_timeout,
            args.enter_timeout,
        )
        <= 0
    ):
        parser.error("all timeout/delay values must be positive")
    if args.hccl_timeout <= args.hang_timeout:
        parser.error("--hccl-timeout must be greater than --hang-timeout")
    return args


def stop_processes(processes: list[mp.Process]) -> None:
    graceful_deadline = time.monotonic() + 3
    for process in processes:
        process.join(timeout=max(0.0, graceful_deadline - time.monotonic()))
    for process in processes:
        if process.is_alive():
            process.terminate()
    for process in processes:
        process.join(timeout=5)
    for process in processes:
        if process.is_alive():
            process.kill()
    for process in processes:
        process.join(timeout=5)


def print_process_snapshot(processes: list[mp.Process]) -> None:
    log("PROCESS SNAPSHOT:")
    for process in processes:
        log(
            f"  name={process.name} pid={process.pid} "
            f"alive={process.is_alive()} exitcode={process.exitcode}"
        )


def dump_live_process_stacks(processes: list[mp.Process]) -> None:
    live = [process for process in processes if process.is_alive() and process.pid]
    if not live:
        return
    log("requesting Python stack dumps from live workers via SIGUSR1")
    for process in live:
        log(f"  SIGUSR1 -> name={process.name} pid={process.pid}")
        try:
            os.kill(process.pid, signal.SIGUSR1)
        except ProcessLookupError:
            pass
    # Give faulthandler time to flush stderr before workers are terminated.
    time.sleep(2)


def print_collective_state(
    entered: list[tuple[Any, ...]],
    done: list[tuple[Any, ...]],
    execution_mode: str,
    compute_started: list[tuple[Any, ...]] | None = None,
    compute_done: list[tuple[Any, ...]] | None = None,
) -> None:
    entered_keys = {(message[1], message[2]) for message in entered}
    done_keys = {(message[1], message[2]) for message in done}
    compute_started_keys = {
        (message[1], message[2]) for message in (compute_started or [])
    }
    compute_done_keys = {(message[1], message[2]) for message in (compute_done or [])}
    log("COLLECTIVE STATE:")
    for model in MODEL_NAMES:
        for rank in range(TP_SIZE):
            key = (model, rank)
            wave = "first" if is_first_wave(model, rank) else "second"
            if key in done_keys:
                state = "DONE"
            elif key in entered_keys:
                state = (
                    "STUCK_IN_ACL_GRAPH_REPLAY"
                    if execution_mode == "aclgraph"
                    else "STUCK_IN_SYNCHRONOUS_ALL_REDUCE"
                )
            elif key in compute_done_keys:
                state = "STUCK_AFTER_COMPUTE_BEFORE_ALL_REDUCE_SUBMIT"
            elif key in compute_started_keys:
                state = "STUCK_IN_COMPUTE"
            else:
                state = "STUCK_BEFORE_COMPUTE_OR_ALL_REDUCE"
            log(f"  model={model} rank={rank} npu={rank} wave={wave} state={state}")


def receive_until(
    status_queue: Any,
    wanted_kind: str,
    wanted_count: int,
    timeout: float,
) -> tuple[list[tuple[Any, ...]], list[tuple[Any, ...]]]:
    wanted: list[tuple[Any, ...]] = []
    other: list[tuple[Any, ...]] = []
    deadline = time.monotonic() + timeout
    while len(wanted) < wanted_count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            message = status_queue.get(timeout=remaining)
        except queue.Empty:
            break
        if message[0] == wanted_kind:
            wanted.append(message)
        else:
            other.append(message)
            if message[0] == "ERROR":
                break
    return wanted, other


def main() -> int:
    args = parse_args()
    if sys.platform != "linux":
        log("ERROR: this reproducer must run inside a Linux Ascend container")
        log("RESULT=UNSUPPORTED_PLATFORM exit_code=2")
        return 2

    # Avoid inheriting an outer torchrun rendezvous configuration.
    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)

    # Keep the CLI readable: GE/CANN INFO and WARNING logs are extremely noisy
    # with 16 workers. Python status lines, tracebacks, RESULT lines, and
    # faulthandler dumps remain visible. Callers can still opt into verbose
    # CANN logging by setting these variables before launch.
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")
    # This reproducer specifically targets contention between AIV-expanded
    # HCCL collectives and model Vector Core operators.
    os.environ["HCCL_OP_EXPANSION_MODE"] = "AIV"

    ctx = mp.get_context("spawn")
    phase_barrier = ctx.Barrier(TP_SIZE * len(MODEL_NAMES))
    race_start = ctx.Event()
    second_wave_start = ctx.Event()
    status_queue = ctx.Queue()
    ports = {"A": find_free_port(), "B": find_free_port()}
    while ports["B"] == ports["A"]:
        ports["B"] = find_free_port()

    log(f"COMMAND: {shlex.join(sys.argv)}")
    log(
        "CONFIG: "
        f"tp={TP_SIZE} execution_mode={args.execution_mode} "
        f"decode_layers={args.decode_layers} tensor_kib={args.tensor_kib} "
        f"compute_mib={args.compute_mib} "
        f"stagger_seconds={args.stagger_seconds} "
        f"hang_timeout={args.hang_timeout} hccl_timeout={args.hccl_timeout} "
        f"ports={ports}"
    )
    log(
        "LOGGING: "
        f"ASCEND_SLOG_PRINT_TO_STDOUT={os.environ['ASCEND_SLOG_PRINT_TO_STDOUT']} "
        f"ASCEND_GLOBAL_LOG_LEVEL={os.environ['ASCEND_GLOBAL_LOG_LEVEL']} "
        f"HCCL_OP_EXPANSION_MODE={os.environ['HCCL_OP_EXPANSION_MODE']}"
    )

    processes: list[mp.Process] = []
    try:
        for model in MODEL_NAMES:
            for rank in range(TP_SIZE):
                process = ctx.Process(
                    target=worker,
                    name=f"model-{model}-rank-{rank}",
                    args=(
                        model,
                        rank,
                        ports[model],
                        args.tensor_kib,
                        args.compute_mib,
                        args.decode_layers,
                        args.execution_mode,
                        phase_barrier,
                        race_start,
                        second_wave_start,
                        status_queue,
                        args.hccl_timeout,
                    ),
                )
                process.start()
                processes.append(process)

        log("started 16 processes: two independent TP=8 groups sharing NPUs 0..7")
        armed, unexpected = receive_until(
            status_queue, "ARMED", len(processes), args.startup_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        if errors or len(armed) != len(processes):
            log(f"SETUP FAILED: armed {len(armed)}/{len(processes)} workers")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=SETUP_FAILED exit_code=2")
            return 2

        log("both HCCL groups warmed up; releasing the crossed first wave")
        race_start.set()
        entered, unexpected = receive_until(
            status_queue, "OP_ENTERED", FIRST_WAVE_SIZE, args.enter_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        if errors or len(entered) != FIRST_WAVE_SIZE:
            log(f"LAUNCH FAILED: first wave entered {len(entered)}/{FIRST_WAVE_SIZE}")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_collective_state(entered, [], args.execution_mode)
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=FIRST_WAVE_LAUNCH_FAILED exit_code=2")
            return 2

        first_wave = sorted((message[1], message[2], message[3]) for message in entered)
        log(f"first wave entered {args.execution_mode}: {first_wave}")
        time.sleep(args.stagger_seconds)
        log("releasing the crossed second wave")
        second_wave_start.set()

        done, unexpected = receive_until(
            status_queue, "DONE", len(processes), args.hang_timeout
        )
        # OP_ENTERED messages from the second wave are expected here.
        errors = [message for message in unexpected if message[0] == "ERROR"]
        second_wave_entered = [
            message for message in unexpected if message[0] == "OP_ENTERED"
        ]
        compute_started = [
            message for message in unexpected if message[0] == "COMPUTE_STARTED"
        ]
        compute_done = [
            message for message in unexpected if message[0] == "COMPUTE_DONE"
        ]
        all_entered = entered + second_wave_entered
        if errors:
            log(f"RUNTIME FAILED with {len(errors)} worker error(s)")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_collective_state(
                all_entered, done, args.execution_mode, compute_started, compute_done
            )
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if len(done) == len(processes):
            log("NO DEADLOCK: all 16 ranks completed both TP=8 all-reduces")
            print_collective_state(
                all_entered, done, args.execution_mode, compute_started, compute_done
            )
            log("RESULT=NO_DEADLOCK exit_code=1")
            return 1

        completed = sorted((message[1], message[2]) for message in done)
        log(
            f"DEADLOCK REPRODUCED: only {len(done)}/16 ranks completed within "
            f"{args.hang_timeout:.1f}s; completed={completed}"
        )
        print_collective_state(
            all_entered, done, args.execution_mode, compute_started, compute_done
        )
        print_process_snapshot(processes)
        dump_live_process_stacks(processes)
        log("RESULT=DEADLOCK_REPRODUCED exit_code=0")
        return 0
    finally:
        # Always release waiters before terminating so setup failures do not
        # leave Python children behind in the container.
        race_start.set()
        second_wave_start.set()
        stop_processes(processes)
        log("CLEANUP complete; final worker exit codes follow")
        print_process_snapshot(processes)


if __name__ == "__main__":
    raise SystemExit(main())
