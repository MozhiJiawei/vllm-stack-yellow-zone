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

Thus the first wave contains four ranks of each all-reduce.  If an incomplete
HCCL operation monopolizes per-device execution resources, the second process
on every NPU cannot run the missing rank and both TP groups wait forever.

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
    tensor_mib: int,
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

        # A modest tensor is enough to launch a real HCCL all-reduce without
        # allocating model parameters. float32 has 262144 elements per MiB.
        tensor = torch.full(
            (tensor_mib * 262_144,),
            float(rank + 1),
            dtype=torch.float32,
            device=f"npu:{device}",
        )

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
        status_queue.put(("ARMED", model, rank, device, ""))
        log(f"ARMED: {label}")
        race_start.wait()

        wave = "first" if is_first_wave(model, rank) else "second"
        if wave == "second":
            second_wave_start.wait()

        log(f"ENTER all_reduce ({wave} wave): {label}")
        work = dist.all_reduce(tensor, async_op=True)
        status_queue.put(("SUBMITTED", model, rank, device, wave))
        log(f"SUBMITTED all_reduce ({wave} wave), waiting: {label}")
        work.wait()
        torch.npu.synchronize()
        log(f"HCCL work completed ({wave} wave): {label}")

        value = float(tensor[0].cpu().item())
        expected = float(TP_SIZE * (TP_SIZE + 1) // 2)
        if value != expected:
            raise RuntimeError(f"wrong all-reduce result: got {value}, expected {expected}")

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
        "--tensor-mib",
        type=int,
        default=4,
        help="all-reduce tensor size per rank in MiB (default: 4)",
    )
    parser.add_argument(
        "--stagger-seconds",
        type=float,
        default=2.0,
        help="delay after all first-wave submissions before releasing the second wave",
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
        "--submit-timeout",
        type=float,
        default=60.0,
        help="seconds allowed for all first-wave async submissions",
    )
    parser.add_argument(
        "--hccl-timeout",
        type=int,
        default=1800,
        help="torch.distributed HCCL timeout; keep above hang-timeout",
    )
    args = parser.parse_args()
    if args.tensor_mib <= 0:
        parser.error("--tensor-mib must be positive")
    if min(args.stagger_seconds, args.hang_timeout, args.startup_timeout, args.submit_timeout) <= 0:
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
    submitted: list[tuple[Any, ...]],
    done: list[tuple[Any, ...]],
) -> None:
    submitted_keys = {(message[1], message[2]) for message in submitted}
    done_keys = {(message[1], message[2]) for message in done}
    log("COLLECTIVE STATE:")
    for model in MODEL_NAMES:
        for rank in range(TP_SIZE):
            key = (model, rank)
            wave = "first" if is_first_wave(model, rank) else "second"
            if key in done_keys:
                state = "DONE"
            elif key in submitted_keys:
                state = "STUCK_AFTER_SUBMIT"
            else:
                state = "STUCK_BEFORE_SUBMIT_RETURNED"
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
        f"tp={TP_SIZE} tensor_mib={args.tensor_mib} "
        f"stagger_seconds={args.stagger_seconds} "
        f"hang_timeout={args.hang_timeout} hccl_timeout={args.hccl_timeout} "
        f"ports={ports}"
    )
    log(
        "LOGGING: "
        f"ASCEND_SLOG_PRINT_TO_STDOUT={os.environ['ASCEND_SLOG_PRINT_TO_STDOUT']} "
        f"ASCEND_GLOBAL_LOG_LEVEL={os.environ['ASCEND_GLOBAL_LOG_LEVEL']}"
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
                        args.tensor_mib,
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
        submitted, unexpected = receive_until(
            status_queue, "SUBMITTED", FIRST_WAVE_SIZE, args.submit_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        if errors or len(submitted) != FIRST_WAVE_SIZE:
            log(f"LAUNCH FAILED: first wave submitted {len(submitted)}/{FIRST_WAVE_SIZE}")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_collective_state(submitted, [])
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=FIRST_WAVE_LAUNCH_FAILED exit_code=2")
            return 2

        first_wave = sorted((message[1], message[2], message[3]) for message in submitted)
        log(f"first wave submitted: {first_wave}")
        time.sleep(args.stagger_seconds)
        log("releasing the crossed second wave")
        second_wave_start.set()

        done, unexpected = receive_until(
            status_queue, "DONE", len(processes), args.hang_timeout
        )
        # SUBMITTED messages from the second wave are expected and ignored.
        errors = [message for message in unexpected if message[0] == "ERROR"]
        second_wave_submitted = [
            message for message in unexpected if message[0] == "SUBMITTED"
        ]
        all_submitted = submitted + second_wave_submitted
        if errors:
            log(f"RUNTIME FAILED with {len(errors)} worker error(s)")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_collective_state(all_submitted, done)
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if len(done) == len(processes):
            log("NO DEADLOCK: all 16 ranks completed both TP=8 all-reduces")
            print_collective_state(all_submitted, done)
            log("RESULT=NO_DEADLOCK exit_code=1")
            return 1

        completed = sorted((message[1], message[2]) for message in done)
        log(
            f"DEADLOCK REPRODUCED: only {len(done)}/16 ranks completed within "
            f"{args.hang_timeout:.1f}s; completed={completed}"
        )
        print_collective_state(all_submitted, done)
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
