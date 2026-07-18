#!/usr/bin/env python3
"""Reproduce the two-model TP=8 xLite/XCCL AIV deadlock.

Run inside a Linux Ascend container with NPUs 0..7 and an installed xLite:

    python3 scripts/repro-tp8-xlite-xccl-deadlock.py

Model A rank N and model B rank N share NPU N.  The first wave launches only
A ranks 0..3 and B ranks 4..7 into xLite's native TP AllReduce.  Those custom
XCCL kernels busy-wait on the missing peers while retaining AIV cores.  The
crossed second wave executes xLite's Qwen3 FlashAttention mixed kernel, which
requests all 20 AIC and their paired 40 AIV on a 910B4, before it can launch
the missing AllReduce ranks:

    NPU 0..3: A AllReduce holds AIV -> B mixed compute waits -> B AR missing
    NPU 4..7: B AllReduce holds AIV -> A mixed compute waits -> A AR missing

This intentionally uses xLite's private runtime stream and native kernels; it
does not construct model parameters.  ``--schedule aligned`` is the normal
control: both models run the selected compute kernel and then enter AllReduce
with all eight TP ranks.  Its exit status is 0 on completion and 1 on an
unexpected hang.  For the default crossed schedule, exit status is 0 when the
hang is observed and 1 when all operations complete.  Setup/runtime failures
always return 2.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import faulthandler
import importlib.metadata
import multiprocessing as mp
import os
import queue
import random
import shlex
import signal
import socket
import sys
import threading
import time
import traceback
from typing import Any


TP_SIZE = 8
MODEL_NAMES = ("A", "B")
WORKER_COUNT = TP_SIZE * len(MODEL_NAMES)
FIRST_WAVE_SIZE = TP_SIZE
XCCL_LARGE_MESSAGE_BYTES_PER_RANK = 327_680
QWEN3_LOCAL_Q_HEADS_TP8 = 8
QWEN3_LOCAL_KV_HEADS_TP8 = 1
QWEN3_HEAD_DIM = 128
XLITE_KV_BLOCK_SIZE = 128
XLITE_FLASH_ATTENTION_TILE = 8192


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def is_first_wave(model: str, rank: int) -> bool:
    return (model == "A" and rank < TP_SIZE // 2) or (
        model == "B" and rank >= TP_SIZE // 2
    )


def ports_are_free(ports: list[int]) -> bool:
    sockets: list[socket.socket] = []
    try:
        for port in ports:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("127.0.0.1", port))
            sockets.append(sock)
        return True
    except OSError:
        return False
    finally:
        for sock in sockets:
            sock.close()


def find_xlite_ports() -> dict[str, int]:
    # xLite uses XLITE_PORT for HCCL and XLITE_PORT+400 for XCCL IPC setup.
    # Reserve non-overlapping candidates for both independent model worlds.
    for _ in range(1_000):
        port_a = random.randint(20_000, 40_000)
        port_b = random.randint(20_000, 40_000)
        required = [port_a, port_a + 400, port_b, port_b + 400]
        if len(set(required)) == len(required) and ports_are_free(required):
            return {"A": port_a, "B": port_b}
    raise RuntimeError("could not find free XLITE_PORT/XCCL port pairs")


def query_device_cores(device: int) -> tuple[int, int]:
    """Return ACL (AIC, AIV) capability counts using only the stdlib."""
    library_name = ctypes.util.find_library("ascendcl") or "libascendcl.so"
    acl = ctypes.CDLL(library_name)
    capability = acl.aclGetDeviceCapability
    capability.argtypes = [
        ctypes.c_uint32,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int64),
    ]
    capability.restype = ctypes.c_int

    values: list[int] = []
    # aclDeviceInfo: ACL_DEVICE_INFO_AI_CORE_NUM=0,
    # ACL_DEVICE_INFO_VECTOR_CORE_NUM=1.
    for info_type in (0, 1):
        value = ctypes.c_int64()
        result = capability(device, info_type, ctypes.byref(value))
        if result != 0:
            raise RuntimeError(
                f"aclGetDeviceCapability(device={device}, type={info_type}) "
                f"failed with ACL error {result}"
            )
        values.append(int(value.value))
    return values[0], values[1]


def close_worker_queue(status_queue: Any) -> None:
    try:
        status_queue.close()
        status_queue.join_thread()
    except BaseException as error:
        log(f"WARNING: failed to flush worker status queue: {error}")


def worker(
    model: str,
    rank: int,
    port: int,
    pool_mib: int,
    decode_tokens: int,
    hidden_size: int,
    dtype_name: str,
    disable_xccl: bool,
    compute_kernel: str,
    attention_cached_tokens: int,
    schedule: str,
    model_a_runtime_ready: Any,
    phase_barrier: Any,
    first_wave_barrier: Any,
    race_start: Any,
    second_wave_start: Any,
    status_queue: Any,
) -> None:
    # Set communicator identity before importing/constructing xLite.  Each
    # model gets an independent TP=8 world even though ranks share devices.
    os.environ["XLITE_NODE_IPS"] = "127.0.0.1"
    os.environ["XLITE_PORT"] = str(port)
    if disable_xccl:
        os.environ["XLITE_DISABLE_XCCL"] = "true"
    else:
        os.environ["XLITE_DISABLE_XCCL"] = "false"
        os.environ.pop("HCCL_DETERMINISTIC", None)

    # Import in spawned children so the parent never initializes ACL/NPU.
    import torch
    import torch_npu
    import xlite
    from xlite._C import Runtime, all_reduce, attention, rmsnorm

    device = rank
    label = f"model={model} rank={rank} npu={device}"
    wave = (
        "aligned"
        if schedule == "aligned"
        else ("first" if is_first_wave(model, rank) else "second")
    )
    faulthandler.enable(file=sys.stderr, all_threads=True)
    faulthandler.register(signal.SIGUSR1, file=sys.stderr, all_threads=True)

    try:
        log(f"INIT begin: {label} pid={os.getpid()} XLITE_PORT={port}")
        if model == "A" and rank == 0:
            try:
                xlite_version = importlib.metadata.version("xlite")
            except importlib.metadata.PackageNotFoundError:
                xlite_version = "unknown"
            log(
                "RUNTIME "
                f"torch={torch.__version__} torch_npu={torch_npu.__version__} "
                f"xlite={xlite_version} xlite_module={xlite.__file__} "
                f"visible_npus={torch.npu.device_count()}"
            )

        # Real dual-vLLM deployments bring the two engines up independently.
        # Serializing communicator initialization avoids testing concurrent
        # HcclCommInitRootInfo setup instead of the intended runtime deadlock.
        if model == "B":
            log(f"INIT waiting for model A runtime group: {label}")
            model_a_runtime_ready.wait()

        # This order matches xLite's own kernel tests. Runtime owns a private
        # ACL stream and size>0 creates the tensor pool required by XCCL.
        runtime = Runtime(device, pool_mib, rank, TP_SIZE, 1)
        torch.npu.set_device(device)
        aic_count, aiv_count = query_device_cores(device)
        if aiv_count <= 0:
            raise RuntimeError(f"invalid AIV count reported by ACL: {aiv_count}")

        dtype = torch.bfloat16 if dtype_name == "bf16" else torch.float16
        dtype_bytes = 2
        tensor_bytes = decode_tokens * hidden_size * dtype_bytes
        bytes_per_rank = (tensor_bytes + TP_SIZE - 1) // TP_SIZE
        allreduce_aiv = min(
            aiv_count,
            TP_SIZE * (2 if bytes_per_rank >= XCCL_LARGE_MESSAGE_BYTES_PER_RANK else 1),
        )
        # xLite rounds the launch count down to a TP-size multiple.
        allreduce_aiv = (allreduce_aiv // TP_SIZE) * TP_SIZE
        if model == "A" and rank == 0:
            log(
                "CORE PLAN "
                f"hardware_aic={aic_count} hardware_aiv={aiv_count} "
                f"xccl_allreduce_aiv={allreduce_aiv} "
                f"compute_kernel={compute_kernel} "
                f"compute_aic={aic_count if compute_kernel == 'flash-attention' else 0} "
                f"compute_aiv={aiv_count} "
                f"bytes_per_tp_rank={bytes_per_rank}"
            )

        comm_input = torch.full(
            (decode_tokens, hidden_size),
            float(rank + 1),
            dtype=dtype,
            device=f"npu:{device}",
        )
        comm_output = torch.empty_like(comm_input)
        compute_input = torch.randn(
            (decode_tokens, hidden_size),
            dtype=dtype,
            device=f"npu:{device}",
        )
        norm_weight = torch.ones((hidden_size,), dtype=dtype, device=f"npu:{device}")
        compute_output = torch.empty_like(compute_input)

        attention_args: tuple[Any, ...] | None = None
        if compute_kernel == "flash-attention":
            query_tokens = 1
            max_num_blocks = (
                attention_cached_tokens + query_tokens + XLITE_KV_BLOCK_SIZE - 1
            ) // XLITE_KV_BLOCK_SIZE
            qkv = torch.randn(
                (
                    query_tokens,
                    (QWEN3_LOCAL_Q_HEADS_TP8 + 2 * QWEN3_LOCAL_KV_HEADS_TP8)
                    * QWEN3_HEAD_DIM,
                ),
                dtype=dtype,
                device=f"npu:{device}",
            )
            k_cache = torch.randn(
                (
                    max_num_blocks,
                    XLITE_KV_BLOCK_SIZE,
                    QWEN3_LOCAL_KV_HEADS_TP8,
                    QWEN3_HEAD_DIM,
                ),
                dtype=dtype,
                device=f"npu:{device}",
            )
            v_cache = torch.randn_like(k_cache)
            attention_output = torch.empty(
                (query_tokens, QWEN3_LOCAL_Q_HEADS_TP8 * QWEN3_HEAD_DIM),
                dtype=dtype,
                device=f"npu:{device}",
            )
            query_start_loc = torch.tensor(
                [0], dtype=torch.int32, device=f"npu:{device}"
            )
            query_lens = torch.tensor(
                [query_tokens], dtype=torch.int32, device=f"npu:{device}"
            )
            cached_lens = torch.tensor(
                [attention_cached_tokens], dtype=torch.int32, device=f"npu:{device}"
            )
            block_tables = torch.arange(
                max_num_blocks, dtype=torch.int32, device=f"npu:{device}"
            )
            attention_args = (
                runtime,
                qkv,
                k_cache,
                v_cache,
                attention_output,
                query_start_loc,
                query_lens,
                cached_lens,
                block_tables,
                QWEN3_LOCAL_Q_HEADS_TP8,
                QWEN3_LOCAL_KV_HEADS_TP8,
                QWEN3_HEAD_DIM,
                XLITE_KV_BLOCK_SIZE,
                1,
                max_num_blocks,
                True,
                XLITE_FLASH_ATTENTION_TILE,
            )

        def run_compute() -> None:
            if compute_kernel == "flash-attention":
                assert attention_args is not None
                attention(*attention_args)
            else:
                rmsnorm(runtime, compute_input, norm_weight, compute_output, 1e-6)

        torch.npu.synchronize()
        log(f"RUNTIME ready: {label} aic={aic_count} aiv={aiv_count}")
        if model == "A":
            status_queue.put(("RUNTIME_READY", model, rank, device, ""))

        # Warm up compute and communication one model at a time. This proves
        # both full TP groups work before deliberately launching partial ones.
        phase_barrier.wait()
        if model == "A":
            log(f"WARMUP begin: {label}")
            run_compute()
            all_reduce(runtime, comm_output, comm_input)
            torch.npu.synchronize()
            log(f"WARMUP done: {label}")
        phase_barrier.wait()
        if model == "B":
            log(f"WARMUP begin: {label}")
            run_compute()
            all_reduce(runtime, comm_output, comm_input)
            torch.npu.synchronize()
            log(f"WARMUP done: {label}")
        phase_barrier.wait()

        expected = float(TP_SIZE * (TP_SIZE + 1) // 2)
        value = float(comm_output[0, 0].cpu().item())
        if value != expected:
            raise RuntimeError(
                f"warm-up all-reduce result mismatch: got {value}, expected {expected}"
            )
        status_queue.put(("ARMED", model, rank, device, ""))
        log(f"ARMED: {label} wave={wave}")
        race_start.wait()

        if schedule == "aligned" or wave == "second":
            if wave == "second":
                second_wave_start.wait()
            status_queue.put(("COMPUTE_STARTED", model, rank, device, wave))
            if compute_kernel == "flash-attention":
                log(
                    "ENTER xLite Qwen3 FlashAttention mixed kernel "
                    f"({aic_count} AIC + {aiv_count} AIV requested) before "
                    f"AllReduce: {label}"
                )
            else:
                log(
                    f"ENTER xLite RMSNorm ({aiv_count} AIV requested) before "
                    f"AllReduce: {label}"
                )
            run_compute()
            status_queue.put(("COMPUTE_DONE", model, rank, device, wave))
            log(f"RETURN xLite {compute_kernel}: {label}")

        if wave == "first":
            # multiprocessing.Queue.put() uses a feeder thread. xLite's pybind
            # AllReduce holds the GIL while synchronizing, so a queued status
            # may not be flushed until the call returns. A process-shared
            # barrier gives the parent synchronous proof that all eight first
            # wave workers are ready before any enters the blocking call.
            log(f"READY at first-wave launch barrier: {label}")
            first_wave_barrier.wait()
        else:
            status_queue.put(("OP_ENTERED", model, rank, device, wave))
        log(
            f"ENTER xLite {'HCCL fallback' if disable_xccl else 'XCCL'} "
            f"AllReduce ({wave} wave): {label}"
        )
        all_reduce(runtime, comm_output, comm_input)
        log(f"RETURN xLite AllReduce ({wave} wave): {label}")

        value = float(comm_output[0, 0].cpu().item())
        if value != expected:
            raise RuntimeError(
                f"race all-reduce result mismatch: got {value}, expected {expected}"
            )
        status_queue.put(("DONE", model, rank, device, wave))
        close_worker_queue(status_queue)
        log(f"DONE: {label}")
    except BaseException:
        error = traceback.format_exc()
        log(f"WORKER ERROR: {label}\n{error}")
        status_queue.put(("ERROR", model, rank, device, error))
        close_worker_queue(status_queue)
        raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pool-mib",
        type=int,
        default=500,
        help="xLite tensor-pool size per process; nonzero enables XCCL (default: 500)",
    )
    parser.add_argument(
        "--decode-tokens",
        type=int,
        default=256,
        help="Qwen3 decode batch; vLLM server default max-num-seqs is 256 (default: 256)",
    )
    parser.add_argument(
        "--hidden-size",
        type=int,
        default=5120,
        help="model hidden dimension; Qwen3-32B is 5120 (default: 5120)",
    )
    parser.add_argument(
        "--dtype",
        choices=("bf16", "fp16"),
        default="bf16",
        help="model dtype; Qwen3-32B config uses BF16 (default: bf16)",
    )
    parser.add_argument(
        "--disable-xccl",
        action="store_true",
        help="control run: force xLite onto its HCCL fallback instead of custom XCCL",
    )
    parser.add_argument(
        "--compute-kernel",
        choices=("flash-attention", "rmsnorm"),
        default="flash-attention",
        help=(
            "compute placed before missing AllReduce ranks; Qwen3 FlashAttention is "
            "the mixed AIC/AIV reproducer, RMSNorm is the pure-AIV control "
            "(default: flash-attention)"
        ),
    )
    parser.add_argument(
        "--attention-cached-tokens",
        type=int,
        default=9216,
        help=(
            "cached sequence length for Qwen3 FlashAttention; must exceed xLite's "
            "8192-token tile to select its mixed flash kernel (default: 9216)"
        ),
    )
    parser.add_argument(
        "--schedule",
        choices=("crossed", "aligned"),
        default="crossed",
        help="crossed deadlock experiment or complete-rank normal control (default: crossed)",
    )
    parser.add_argument(
        "--init-stagger-seconds",
        type=float,
        default=5.0,
        help="gap between model A and B communicator initialization (default: 5)",
    )
    parser.add_argument(
        "--stagger-seconds",
        type=float,
        default=5.0,
        help="delay after partial AllReduce entry before releasing compute (default: 5)",
    )
    parser.add_argument(
        "--hang-timeout",
        type=float,
        default=30.0,
        help="seconds after second-wave release before declaring a deadlock (default: 30)",
    )
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=600.0,
        help="seconds allowed for runtime init and sequential warm-ups (default: 600)",
    )
    parser.add_argument(
        "--enter-timeout",
        type=float,
        default=60.0,
        help="seconds allowed for all first-wave ranks to enter AllReduce (default: 60)",
    )
    args = parser.parse_args()
    for name in (
        "pool_mib",
        "decode_tokens",
        "hidden_size",
        "attention_cached_tokens",
    ):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if (
        args.compute_kernel == "flash-attention"
        and args.attention_cached_tokens <= XLITE_FLASH_ATTENTION_TILE
    ):
        parser.error(
            "--attention-cached-tokens must exceed 8192 for the mixed "
            "FlashAttention path"
        )
    for name in (
        "stagger_seconds",
        "init_stagger_seconds",
        "hang_timeout",
        "startup_timeout",
        "enter_timeout",
    ):
        if getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    return args


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


def stop_processes(processes: list[mp.Process]) -> None:
    for process in processes:
        process.join(timeout=0.2)
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
        try:
            log(f"  SIGUSR1 -> name={process.name} pid={process.pid}")
            os.kill(process.pid, signal.SIGUSR1)
        except ProcessLookupError:
            pass
    time.sleep(2)


def print_operation_state(
    entered: list[tuple[Any, ...]],
    done: list[tuple[Any, ...]],
    compute_started: list[tuple[Any, ...]],
    compute_done: list[tuple[Any, ...]],
    schedule: str,
) -> None:
    entered_keys = {(message[1], message[2]) for message in entered}
    done_keys = {(message[1], message[2]) for message in done}
    compute_started_keys = {(message[1], message[2]) for message in compute_started}
    compute_done_keys = {(message[1], message[2]) for message in compute_done}
    log("OPERATION STATE:")
    for model in MODEL_NAMES:
        for rank in range(TP_SIZE):
            key = (model, rank)
            wave = (
                "aligned"
                if schedule == "aligned"
                else ("first" if is_first_wave(model, rank) else "second")
            )
            if key in done_keys:
                state = "DONE"
            elif key in entered_keys:
                state = "STUCK_IN_XLITE_ALL_REDUCE"
            elif key in compute_started_keys and key not in compute_done_keys:
                state = "STUCK_IN_XLITE_COMPUTE"
            elif key in compute_done_keys:
                state = "BETWEEN_COMPUTE_AND_ALL_REDUCE"
            else:
                state = "NOT_RELEASED_OR_FAILED"
            log(f"  model={model} rank={rank} npu={rank} wave={wave} state={state}")


def main() -> int:
    args = parse_args()
    if sys.platform != "linux":
        log("ERROR: this reproducer must run inside a Linux Ascend container")
        log("RESULT=UNSUPPORTED_PLATFORM exit_code=2")
        return 2

    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")

    try:
        ports = find_xlite_ports()
    except BaseException:
        log(f"PORT SETUP ERROR:\n{traceback.format_exc()}")
        log("RESULT=SETUP_FAILED exit_code=2")
        return 2

    ctx = mp.get_context("spawn")
    phase_barrier = ctx.Barrier(WORKER_COUNT)
    first_wave_barrier = ctx.Barrier(FIRST_WAVE_SIZE + 1)
    model_a_runtime_ready = ctx.Event()
    race_start = ctx.Event()
    second_wave_start = ctx.Event()
    status_queue = ctx.Queue()

    tensor_bytes = args.decode_tokens * args.hidden_size * 2
    bytes_per_rank = (tensor_bytes + TP_SIZE - 1) // TP_SIZE
    expected_xccl_cap = TP_SIZE * (
        2 if bytes_per_rank >= XCCL_LARGE_MESSAGE_BYTES_PER_RANK else 1
    )
    log(f"COMMAND: {shlex.join(sys.argv)}")
    log(
        "CONFIG: "
        f"tp={TP_SIZE} backend={'HCCL_FALLBACK' if args.disable_xccl else 'XCCL'} "
        f"schedule={args.schedule} "
        f"model=Qwen3-32B pool_mib={args.pool_mib} dtype={args.dtype} "
        f"op_shape=({args.decode_tokens},{args.hidden_size}) "
        f"compute_kernel={args.compute_kernel} "
        f"attention_cached_tokens={args.attention_cached_tokens} "
        f"tensor_kib={tensor_bytes // 1024} "
        f"xccl_allreduce_aiv_cap={expected_xccl_cap} ports={ports} "
        f"init_stagger_seconds={args.init_stagger_seconds} "
        f"stagger_seconds={args.stagger_seconds} hang_timeout={args.hang_timeout}"
    )
    log(
        "LOGGING: "
        f"ASCEND_SLOG_PRINT_TO_STDOUT={os.environ['ASCEND_SLOG_PRINT_TO_STDOUT']} "
        f"ASCEND_GLOBAL_LOG_LEVEL={os.environ['ASCEND_GLOBAL_LOG_LEVEL']}"
    )

    processes: list[mp.Process] = []
    entered: list[tuple[Any, ...]] = []
    done: list[tuple[Any, ...]] = []
    compute_started: list[tuple[Any, ...]] = []
    compute_done: list[tuple[Any, ...]] = []
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
                        args.pool_mib,
                        args.decode_tokens,
                        args.hidden_size,
                        args.dtype,
                        args.disable_xccl,
                        args.compute_kernel,
                        args.attention_cached_tokens,
                        args.schedule,
                        model_a_runtime_ready,
                        phase_barrier,
                        first_wave_barrier,
                        race_start,
                        second_wave_start,
                        status_queue,
                    ),
                )
                process.start()
                processes.append(process)

        log("started 16 processes: initializing model A xLite TP=8 runtime first")
        runtime_ready, unexpected = receive_until(
            status_queue, "RUNTIME_READY", TP_SIZE, args.startup_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        if errors or len(runtime_ready) != TP_SIZE:
            log(f"MODEL A INIT FAILED: ready {len(runtime_ready)}/{TP_SIZE} runtimes")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=SETUP_FAILED exit_code=2")
            return 2

        log(
            f"model A runtime group ready; waiting {args.init_stagger_seconds:.1f}s "
            "before model B communicator initialization"
        )
        time.sleep(args.init_stagger_seconds)
        log("allowing model B communicator initialization")
        model_a_runtime_ready.set()
        armed, unexpected = receive_until(
            status_queue, "ARMED", WORKER_COUNT, args.startup_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        if errors or len(armed) != WORKER_COUNT:
            log(f"SETUP FAILED: armed {len(armed)}/{WORKER_COUNT} workers")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=SETUP_FAILED exit_code=2")
            return 2

        if args.schedule == "aligned":
            log(
                "NORMAL CONTROL: both xLite TP groups warmed up; releasing "
                "all 16 workers into compute followed by complete TP8 AllReduce"
            )
            race_start.set()
            done, unexpected = receive_until(
                status_queue, "DONE", WORKER_COUNT, args.hang_timeout
            )
            errors = [message for message in unexpected if message[0] == "ERROR"]
            entered = [message for message in unexpected if message[0] == "OP_ENTERED"]
            compute_started = [
                message for message in unexpected if message[0] == "COMPUTE_STARTED"
            ]
            compute_done = [
                message for message in unexpected if message[0] == "COMPUTE_DONE"
            ]
            if errors:
                log(f"NORMAL CONTROL FAILED with {len(errors)} worker error(s)")
                for message in errors:
                    log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
                print_operation_state(
                    entered,
                    done,
                    compute_started,
                    compute_done,
                    args.schedule,
                )
                print_process_snapshot(processes)
                dump_live_process_stacks(processes)
                log("RESULT=NORMAL_CONTROL_RUNTIME_FAILED exit_code=2")
                return 2
            if len(done) == WORKER_COUNT:
                log("NORMAL CONTROL PASSED: all 16 xLite workers completed")
                print_operation_state(
                    entered,
                    done,
                    compute_started,
                    compute_done,
                    args.schedule,
                )
                log("RESULT=NORMAL_CONTROL_PASSED exit_code=0")
                return 0

            log(
                f"NORMAL CONTROL HUNG: only {len(done)}/{WORKER_COUNT} workers "
                f"completed within {args.hang_timeout:.1f}s"
            )
            print_operation_state(
                entered,
                done,
                compute_started,
                compute_done,
                args.schedule,
            )
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=NORMAL_CONTROL_HUNG exit_code=1")
            return 1

        log("both xLite TP groups warmed up; releasing partial XCCL first wave")
        race_start.set()
        try:
            first_wave_barrier.wait(timeout=args.enter_timeout)
        except threading.BrokenBarrierError:
            log(
                "LAUNCH FAILED: not all eight first-wave workers reached the "
                f"synchronous launch barrier within {args.enter_timeout:.1f}s"
            )
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=FIRST_WAVE_BARRIER_FAILED exit_code=2")
            return 2

        # The shared barrier, unlike multiprocessing.Queue's background feeder,
        # is synchronous proof that these exact workers have been released into
        # the pybind AllReduce call.
        entered = [
            ("OP_ENTERED", model, rank, rank, "first")
            for model in MODEL_NAMES
            for rank in range(TP_SIZE)
            if is_first_wave(model, rank)
        ]
        first_wave = sorted((message[1], message[2], message[3]) for message in entered)
        log(f"partial AllReduce first wave synchronously released: {first_wave}")
        time.sleep(args.stagger_seconds)
        log(f"releasing crossed second wave into xLite {args.compute_kernel}")
        second_wave_start.set()

        done, unexpected = receive_until(
            status_queue, "DONE", WORKER_COUNT, args.hang_timeout
        )
        errors = [message for message in unexpected if message[0] == "ERROR"]
        second_entered = [
            message for message in unexpected if message[0] == "OP_ENTERED"
        ]
        compute_started = [
            message for message in unexpected if message[0] == "COMPUTE_STARTED"
        ]
        compute_done = [
            message for message in unexpected if message[0] == "COMPUTE_DONE"
        ]
        # Queue feeder threads cannot flush COMPUTE_STARTED while a native
        # xLite call holds the GIL. The event release and fixed worker control
        # flow establish that all second-wave workers were scheduled into it.
        compute_started.extend(
            ("COMPUTE_STARTED", model, rank, rank, "second")
            for model in MODEL_NAMES
            for rank in range(TP_SIZE)
            if not is_first_wave(model, rank)
        )
        all_entered = entered + second_entered
        if errors:
            log(f"RUNTIME FAILED with {len(errors)} worker error(s)")
            for message in errors:
                log(f"{message[1]} rank {message[2]} error:\n{message[4]}")
            print_operation_state(
                all_entered,
                done,
                compute_started,
                compute_done,
                args.schedule,
            )
            print_process_snapshot(processes)
            dump_live_process_stacks(processes)
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if len(done) == WORKER_COUNT:
            log("NO DEADLOCK: all 16 xLite workers completed")
            print_operation_state(
                all_entered,
                done,
                compute_started,
                compute_done,
                args.schedule,
            )
            log("RESULT=NO_DEADLOCK exit_code=1")
            return 1

        log(
            f"DEADLOCK REPRODUCED: only {len(done)}/{WORKER_COUNT} workers "
            f"completed within {args.hang_timeout:.1f}s"
        )
        print_operation_state(
            all_entered,
            done,
            compute_started,
            compute_done,
            args.schedule,
        )
        print_process_snapshot(processes)
        dump_live_process_stacks(processes)
        log("RESULT=DEADLOCK_REPRODUCED exit_code=0")
        return 0
    finally:
        try:
            first_wave_barrier.abort()
        except BaseException:
            pass
        model_a_runtime_ready.set()
        race_start.set()
        second_wave_start.set()
        stop_processes(processes)
        log("CLEANUP complete; final worker exit codes follow")
        print_process_snapshot(processes)


if __name__ == "__main__":
    raise SystemExit(main())
