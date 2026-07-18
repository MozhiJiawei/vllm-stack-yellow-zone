#!/usr/bin/env python3
"""Reproduce the physical 910B4 two-model TP8 xLite/XCCL deadlock.

The crossed schedule starts incomplete collectives on opposite halves:

    NPU 0..3: model A XCCL AllReduce -> model B FlashAttention -> B AllReduce
    NPU 4..7: model B XCCL AllReduce -> model A FlashAttention -> A AllReduce

Each incomplete XCCL collective retains Vector Cores while waiting for four
missing ranks.  The missing ranks first execute the same xLite Qwen3-32B
FlashAttention kernel used by decode.  If that mixed AIC/AIV kernel cannot be
scheduled, neither model can submit its missing ranks: hold-and-wait.

This is a kernel-level reproducer.  It needs torch, torch-npu and xLite, but no
model checkpoint or vCANN.  ``--schedule aligned`` is the normal control.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import multiprocessing as mp
from multiprocessing.connection import Connection, wait
import os
import random
import shlex
import socket
import sys
import time
import traceback
from typing import Any


TP = 8
MODELS = ("A", "B")
WORKERS = 16
Q_HEADS_TP8 = 8
KV_HEADS_TP8 = 1
HEAD_DIM = 128
BLOCK_SIZE = 128
FLASH_TILE = 8192


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def first_wave(model: str, rank: int) -> bool:
    return (model == "A" and rank < 4) or (model == "B" and rank >= 4)


def free_ports() -> tuple[int, int]:
    """Find two non-overlapping xLite base ports (xLite also uses port+400)."""
    for _ in range(1000):
        ports = (random.randint(20000, 39000), random.randint(20000, 39000))
        candidates = (ports[0], ports[0] + 400, ports[1], ports[1] + 400)
        if len(set(candidates)) != 4:
            continue
        sockets: list[socket.socket] = []
        try:
            for port in candidates:
                sock = socket.socket()
                sock.bind(("127.0.0.1", port))
                sockets.append(sock)
            return ports
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("cannot find two free XLITE_PORT pairs")


def make_attention(
    torch: Any,
    device: int,
    runtime: Any,
    batch: int,
    cached_tokens: int,
    dtype: Any,
) -> tuple[Any, ...]:
    """Build xLite's real batch-N decode metadata: one query per request."""
    max_blocks = (cached_tokens + 1 + BLOCK_SIZE - 1) // BLOCK_SIZE
    cache_blocks = batch * max_blocks
    qkv_width = (Q_HEADS_TP8 + 2 * KV_HEADS_TP8) * HEAD_DIM
    dev = f"npu:{device}"

    qkv = torch.randn((batch, qkv_width), dtype=dtype, device=dev)
    k_cache = torch.randn(
        (cache_blocks, BLOCK_SIZE, KV_HEADS_TP8, HEAD_DIM),
        dtype=dtype,
        device=dev,
    )
    v_cache = torch.randn_like(k_cache)
    output = torch.empty(
        (batch, Q_HEADS_TP8 * HEAD_DIM), dtype=dtype, device=dev
    )

    # One decode token per request.  Each request owns a separate block-table
    # row, flattened exactly like xLite's upstream attention kernel test.
    query_start = torch.arange(batch, dtype=torch.int32, device=dev)
    query_lens = torch.ones(batch, dtype=torch.int32, device=dev)
    cached_lens = torch.full(
        (batch,), cached_tokens, dtype=torch.int32, device=dev
    )
    block_tables = torch.arange(
        cache_blocks, dtype=torch.int32, device=dev
    )

    return (
        runtime,
        qkv,
        k_cache,
        v_cache,
        output,
        query_start,
        query_lens,
        cached_lens,
        block_tables,
        Q_HEADS_TP8,
        KV_HEADS_TP8,
        HEAD_DIM,
        BLOCK_SIZE,
        batch,
        max_blocks,
        True,
        FLASH_TILE,
    )


def worker(
    model: str,
    rank: int,
    port: int,
    args: argparse.Namespace,
    model_a_ready: Any,
    phases: Any,
    first_barrier: Any,
    start: Any,
    start_compute: Any,
    status: Connection,
) -> None:
    label = f"model={model} rank={rank} npu={rank}"
    try:
        os.environ["XLITE_NODE_IPS"] = "127.0.0.1"
        os.environ["XLITE_PORT"] = str(port)
        os.environ["XLITE_DISABLE_XCCL"] = "false"
        os.environ.pop("HCCL_DETERMINISTIC", None)

        import torch
        import torch_npu  # noqa: F401 - registers the NPU backend
        import xlite
        from xlite._C import Runtime, all_reduce, attention

        if model == "A" and rank == 0:
            try:
                version = importlib.metadata.version("xlite")
            except importlib.metadata.PackageNotFoundError:
                version = "unknown"
            log(
                f"RUNTIME torch={torch.__version__} torch_npu={torch_npu.__version__} "
                f"xlite={version} module={xlite.__file__}"
            )

        if model == "B":
            model_a_ready.wait()

        runtime = Runtime(rank, args.pool_mib, rank, TP, 1)
        torch.npu.set_device(rank)
        dtype = torch.bfloat16 if args.dtype == "bf16" else torch.float16
        dev = f"npu:{rank}"
        comm_in = torch.full(
            (args.batch, args.hidden_size), rank + 1, dtype=dtype, device=dev
        )
        comm_out = torch.empty_like(comm_in)
        attention_args = make_attention(
            torch, rank, runtime, args.batch, args.cached_tokens, dtype
        )
        torch.npu.synchronize()
        status.send(("READY", model, rank, label))

        # Initialize the two TP groups independently, matching two vLLM engines.
        # Warm-up also proves the exact batch-16 FA and both complete TP8 groups.
        phases.wait()
        if model == "A":
            attention(*attention_args)
            all_reduce(runtime, comm_out, comm_in)
            torch.npu.synchronize()
        phases.wait()
        if model == "B":
            attention(*attention_args)
            all_reduce(runtime, comm_out, comm_in)
            torch.npu.synchronize()
        phases.wait()

        expected = float(TP * (TP + 1) // 2)
        actual = float(comm_out[0, 0].cpu().item())
        if actual != expected:
            raise RuntimeError(
                f"warm-up AllReduce mismatch: got {actual}, expected {expected}"
            )
        status.send(("ARMED", model, rank, label))
        start.wait()

        wave = "aligned" if args.schedule == "aligned" else (
            "first" if first_wave(model, rank) else "second"
        )
        if wave != "first":
            if wave == "second":
                start_compute.wait()
            status.send(("COMPUTE_ENTER", model, rank, label))
            log(
                f"ENTER xLite FlashAttention batch={args.batch} "
                f"cached={args.cached_tokens}: {label}"
            )
            attention(*attention_args)
            status.send(("COMPUTE_DONE", model, rank, label))
            log(f"RETURN xLite FlashAttention: {label}")

        if wave == "first":
            first_barrier.wait()
        status.send(("AR_ENTER", model, rank, label))
        log(f"ENTER xLite XCCL AllReduce wave={wave}: {label}")
        all_reduce(runtime, comm_out, comm_in)
        status.send(("AR_DONE", model, rank, label))
        log(f"RETURN xLite XCCL AllReduce wave={wave}: {label}")

        actual = float(comm_out[0, 0].cpu().item())
        if actual != expected:
            raise RuntimeError(
                f"race AllReduce mismatch: got {actual}, expected {expected}"
            )
        status.send(("DONE", model, rank, label))
    except BaseException:
        error = traceback.format_exc()
        log(f"WORKER ERROR: {label}\n{error}")
        try:
            status.send(("ERROR", model, rank, error))
        except BaseException:
            pass
        raise
    finally:
        status.close()


class StatusCollector:
    def __init__(self, connections: list[Connection]) -> None:
        self.connections = connections
        self.messages: list[tuple[Any, ...]] = []
        self.states: dict[tuple[str, int], str] = {}

    def count(self, kind: str) -> int:
        return sum(message[0] == kind for message in self.messages)

    def errors(self) -> list[tuple[Any, ...]]:
        return [message for message in self.messages if message[0] == "ERROR"]

    def until(self, kind: str, count: int, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while self.count(kind) < count and not self.errors():
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.connections:
                break
            for connection in wait(self.connections, timeout=remaining):
                try:
                    message = connection.recv()
                except EOFError:
                    self.connections.remove(connection)
                    continue
                self.messages.append(message)
                self.states[(message[1], message[2])] = message[0]
        return self.count(kind) >= count

    def print_states(self) -> None:
        log("OPERATION STATE:")
        for model in MODELS:
            for rank in range(TP):
                state = self.states.get((model, rank), "NO_STATUS")
                log(f"  model={model} rank={rank} npu={rank} state={state}")


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


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--schedule", choices=("crossed", "aligned"), default="crossed")
    result.add_argument("--batch", type=int, default=16, help="decode requests (default: 16)")
    result.add_argument("--hidden-size", type=int, default=5120)
    result.add_argument("--cached-tokens", type=int, default=9216)
    result.add_argument("--dtype", choices=("bf16", "fp16"), default="bf16")
    result.add_argument("--pool-mib", type=int, default=500)
    result.add_argument("--init-stagger-seconds", type=float, default=10)
    result.add_argument("--stagger-seconds", type=float, default=5)
    result.add_argument("--hang-timeout", type=float, default=30)
    result.add_argument("--startup-timeout", type=float, default=600)
    return result


def main() -> int:
    args = parser().parse_args()
    if sys.platform != "linux":
        log("ERROR: run this script inside the Linux Ascend container")
        log("RESULT=UNSUPPORTED_PLATFORM exit_code=2")
        return 2
    if args.batch <= 0 or args.hidden_size <= 0 or args.pool_mib <= 0:
        parser().error("batch, hidden-size and pool-mib must be positive")
    if args.cached_tokens <= FLASH_TILE:
        parser().error("cached-tokens must exceed 8192 to select FlashAttention")

    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")

    try:
        port_a, port_b = free_ports()
    except BaseException:
        log(f"SETUP ERROR:\n{traceback.format_exc()}")
        log("RESULT=SETUP_FAILED exit_code=2")
        return 2

    tensor_bytes = args.batch * args.hidden_size * 2
    # xLite XCCL uses TP AIVs for this small batch-16 message on 910B4.
    expected_ar_aiv = TP if tensor_bytes // TP < 327_680 else TP * 2
    log(f"COMMAND: {shlex.join(sys.argv)}")
    log(
        f"CONFIG: physical_910B4=true tp=8 schedule={args.schedule} "
        f"decode_batch={args.batch} hidden_size={args.hidden_size} dtype={args.dtype} "
        f"cached_tokens={args.cached_tokens} comm_bytes={tensor_bytes} "
        f"expected_xccl_aiv={expected_ar_aiv} expected_fa=20AIC+40AIV "
        f"ports=A:{port_a},B:{port_b}"
    )

    ctx = mp.get_context("spawn")
    model_a_ready = ctx.Event()
    phases = ctx.Barrier(WORKERS)
    first_barrier = ctx.Barrier(TP)
    start = ctx.Event()
    start_compute = ctx.Event()
    processes: list[mp.Process] = []
    parents: list[Connection] = []

    try:
        for model, port in (("A", port_a), ("B", port_b)):
            for rank in range(TP):
                parent, child = ctx.Pipe(duplex=False)
                process = ctx.Process(
                    target=worker,
                    name=f"model-{model}-rank-{rank}",
                    args=(
                        model, rank, port, args, model_a_ready, phases,
                        first_barrier, start, start_compute, child,
                    ),
                )
                process.start()
                child.close()
                processes.append(process)
                parents.append(parent)

        collector = StatusCollector(parents.copy())
        log("started 16 workers; initializing model A TP8 before model B")
        if not collector.until("READY", TP, args.startup_timeout):
            collector.print_states()
            log("RESULT=SETUP_FAILED exit_code=2")
            return 2
        time.sleep(args.init_stagger_seconds)
        model_a_ready.set()
        if not collector.until("ARMED", WORKERS, args.startup_timeout):
            collector.print_states()
            log("RESULT=SETUP_FAILED exit_code=2")
            return 2

        if args.schedule == "aligned":
            log("CONTROL: releasing two complete TP8 groups")
            start.set()
            complete = collector.until("DONE", WORKERS, args.hang_timeout)
            collector.print_states()
            if collector.errors():
                log("RESULT=NORMAL_CONTROL_RUNTIME_FAILED exit_code=2")
                return 2
            if complete:
                log("RESULT=NORMAL_CONTROL_PASSED exit_code=0")
                return 0
            log("RESULT=NORMAL_CONTROL_HUNG exit_code=1")
            return 1

        log("CROSSED: releasing 8 incomplete XCCL AllReduce ranks")
        start.set()
        if not collector.until("AR_ENTER", TP, args.hang_timeout):
            collector.print_states()
            log("RESULT=FIRST_WAVE_FAILED exit_code=2")
            return 2
        time.sleep(args.stagger_seconds)
        log("CROSSED: releasing 8 missing ranks into batch-16 FlashAttention")
        start_compute.set()
        complete = collector.until("DONE", WORKERS, args.hang_timeout)
        collector.print_states()
        if collector.errors():
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if complete:
            log("RESULT=NO_DEADLOCK exit_code=1")
            return 1
        log(
            f"DEADLOCK: done={collector.count('DONE')}/16 "
            f"compute_enter={collector.count('COMPUTE_ENTER')}/8 "
            f"compute_done={collector.count('COMPUTE_DONE')}/8"
        )
        log("RESULT=DEADLOCK_REPRODUCED exit_code=0")
        return 0
    finally:
        model_a_ready.set()
        start.set()
        start_compute.set()
        try:
            phases.abort()
            first_barrier.abort()
        except BaseException:
            pass
        stop(processes)
        for connection in parents:
            connection.close()
        log("CLEANUP complete")


if __name__ == "__main__":
    raise SystemExit(main())
