#!/usr/bin/env python3
"""Reproduce the physical 910B4 two-model TP8 xLite/XCCL deadlock.

The crossed schedule submits complete native forwards in opposite card order:

    NPU 0..3: submit complete model A forward, then complete model B forward
    NPU 4..7: submit complete model B forward, then complete model A forward

Every rank submits the entire 64-layer model exactly once.  The first forwards
naturally reach their first TP AllReduce with four ranks missing.  Only then are
the other model's complete forwards released on the same cards.  There are no
standalone held/tail collectives and no deliberately omitted later ranks.

All ranks call xLite Model.forward_with_inputs_embeds, which submits the real
Qwen3-32B decode layer sequence (QKV, FlashAttention, O projection, AllReduce,
MLP, AllReduce) on xLite's private stream.  If a naturally waiting collective
retains the AIVs needed by the opposite model's compute prefix, that model can
never reach the missing-rank collective: hold-and-wait.

No checkpoint is needed: all layers share one correctly-shaped set of synthetic
BF16 weights and one KV-cache pair.  It needs torch, torch-npu and xLite, but no
vCANN.  ``--schedule aligned`` is the normal control.
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
QWEN3_INTERMEDIATE = 25600
QWEN3_HEADS = 64
QWEN3_KV_HEADS = 8


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


def make_model_forward(
    torch: Any,
    device: int,
    runtime: Any,
    batch: int,
    cached_tokens: int,
    dtype: Any,
    layers: int,
    hidden_size: int,
    intermediate_size: int,
    Model: Any,
    ModelConfig: Any,
    AttnMeta: Any,
    AttnMHA: Any,
    tp_size: int = TP,
) -> tuple[Any, ...]:
    """Build a checkpoint-free native xLite Qwen3 dense decode forward."""
    if tp_size <= 0:
        raise ValueError(f"tp_size must be positive: {tp_size}")
    if QWEN3_HEADS % tp_size or QWEN3_KV_HEADS % tp_size:
        raise ValueError(
            f"Qwen3 heads must divide evenly across TP={tp_size}: "
            f"heads={QWEN3_HEADS} kv_heads={QWEN3_KV_HEADS}"
        )
    if intermediate_size % tp_size:
        raise ValueError(
            f"intermediate_size must divide evenly across TP={tp_size}: "
            f"intermediate_size={intermediate_size}"
        )

    local_q_heads = QWEN3_HEADS // tp_size
    local_kv_heads = QWEN3_KV_HEADS // tp_size
    max_blocks = (cached_tokens + 1 + BLOCK_SIZE - 1) // BLOCK_SIZE
    cache_blocks = batch * max_blocks
    dev = f"npu:{device}"

    config = ModelConfig()
    config.vocab_size = tp_size
    config.hidden_size = hidden_size
    config.n_layers = layers
    config.n_heads = QWEN3_HEADS
    config.n_kv_heads = QWEN3_KV_HEADS
    config.head_dim = HEAD_DIM
    config.rope_head_dim = HEAD_DIM
    config.norm_eps = 1e-6
    config.rope_theta = 1_000_000.0
    config.softmax_scale = HEAD_DIM ** -0.5
    config.n_dense_layers = layers
    config.intermediate_size = intermediate_size
    config.def_tp_size = tp_size
    config.def_dp_size = 1
    config.moe_ep_size = 1
    config.moe_tp_size = 1
    config.max_seq_len = cached_tokens + 1
    config.max_batch_size = batch
    # vLLM-Ascend sets max_m (batch in decode-only graph mode).
    config.max_m = batch
    config.block_size = BLOCK_SIZE
    config.weight_nz = False
    config.qkv_bias = False
    config.qk_norm = True
    config.attn_type = AttnMHA

    local_intermediate = intermediate_size // tp_size
    local_qkv = (local_q_heads + 2 * local_kv_heads) * HEAD_DIM
    # The list length preserves the complete layer stack. Reusing storage keeps
    # this reproducer small without changing any native Model.forward operator.
    norm = torch.ones(hidden_size, dtype=dtype, device=dev)
    q_norm = torch.ones(HEAD_DIM, dtype=dtype, device=dev)
    k_norm = torch.ones(HEAD_DIM, dtype=dtype, device=dev)
    qkv = torch.zeros((local_qkv, hidden_size), dtype=dtype, device=dev)
    attn_out = torch.zeros(
        (hidden_size, local_q_heads * HEAD_DIM), dtype=dtype, device=dev
    )
    up_gate = torch.zeros(
        (2 * local_intermediate, hidden_size), dtype=dtype, device=dev
    )
    down = torch.zeros(
        (hidden_size, local_intermediate), dtype=dtype, device=dev
    )
    embed = torch.zeros((1, hidden_size), dtype=dtype, device=dev)

    model = Model()
    model.embed = embed
    model.norm = norm
    model.head = embed
    model.attn_norm = [norm] * layers
    model.attn_out = [attn_out] * layers
    model.mha_qkv = [qkv] * layers
    model.mha_q_norm = [q_norm] * layers
    model.mha_k_norm = [k_norm] * layers
    model.mlp_norm = [norm] * layers
    model.mlp_up_gate = [up_gate] * layers
    model.mlp_down = [down] * layers
    model.init(config, device)

    pool_mib = model.get_tensor_pool_size()
    if runtime.init_tensor_pool(pool_mib) != 0:
        raise RuntimeError(f"xLite tensor pool init failed: requested_mib={pool_mib}")

    k_cache = torch.zeros(
        (cache_blocks, BLOCK_SIZE, local_kv_heads, HEAD_DIM),
        dtype=dtype,
        device=dev,
    )
    v_cache = torch.zeros_like(k_cache)
    kv_cache = [[k_cache, v_cache] for _ in range(layers)]

    attn_meta = AttnMeta()
    attn_meta.lens = [1] * batch
    attn_meta.cached_lens = [cached_tokens] * batch
    attn_meta.is_prefills = [False] * batch
    attn_meta.block_tables_cpu = [
        [request * max_blocks + block for block in range(max_blocks)]
        for request in range(batch)
    ]
    attn_meta.positions = torch.full(
        (batch,), cached_tokens, dtype=torch.int64, device=dev
    )

    inputs = torch.ones((batch, hidden_size), dtype=dtype, device=dev)
    output = torch.empty_like(inputs)
    # xLite expects concatenated cos/sin, hence width=head_dim rather than 2x.
    freqs_cis = torch.ones(
        (cached_tokens + 1, HEAD_DIM), dtype=dtype, device=dev
    )
    stream = torch.npu.current_stream().npu_stream
    return model, runtime, inputs, attn_meta, kv_cache, freqs_cis, output, stream, []


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
        from xlite._C import (
            AttnMHA,
            Model,
            AttnMeta,
            ModelConfig,
            Runtime,
        )

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

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 0, rank, TP, 1)
        dtype = torch.bfloat16 if args.dtype == "bf16" else torch.float16
        forward_args = make_model_forward(
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

        # Initialize the two TP groups independently, matching two vLLM engines.
        # Warm up each complete TP8 native forward independently.
        phases.wait()
        if model == "A":
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            torch.npu.synchronize()
        phases.wait()
        if model == "B":
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            torch.npu.synchronize()
        phases.wait()

        if not bool(torch.isfinite(forward_args[6]).all().cpu().item()):
            raise RuntimeError("warm-up native Model.forward produced non-finite output")
        status.send(("ARMED", model, rank, label))
        start.wait()

        wave = "aligned" if args.schedule == "aligned" else (
            "first" if first_wave(model, rank) else "second"
        )
        if wave == "aligned":
            status.send(("MODEL_ENTER_ALIGNED", model, rank, label))
            log(f"ENTER xLite Model.forward layers={args.layers}: {label}")
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_ALIGNED", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN xLite Model.forward: {label}")
            status.send(("DONE", model, rank, label))
            return

        if wave == "first":
            first_barrier.wait()
            status.send(("MODEL_ENTER_FIRST", model, rank, label))
            log(f"ENTER first complete xLite Model.forward layers={args.layers}: {label}")
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_FIRST", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN first complete xLite Model.forward: {label}")
        else:
            start_compute.wait()
            status.send(("MODEL_ENTER_SECOND", model, rank, label))
            log(f"ENTER second complete xLite Model.forward layers={args.layers}: {label}")
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            status.send(("MODEL_SUBMITTED_SECOND", model, rank, label))
            torch.npu.synchronize()
            status.send(("MODEL_DONE", model, rank, label))
            log(f"RETURN second complete xLite Model.forward: {label}")

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
    result.add_argument("--intermediate-size", type=int, default=QWEN3_INTERMEDIATE)
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
    if sys.platform != "linux":
        log("ERROR: run this script inside the Linux Ascend container")
        log("RESULT=UNSUPPORTED_PLATFORM exit_code=2")
        return 2
    if args.batch <= 0 or args.layers <= 0 or args.intermediate_size <= 0:
        parser().error("batch, layers and intermediate-size must be positive")
    if args.hidden_size != 5120:
        parser().error("this Qwen3-32B reproducer requires --hidden-size 5120")
    if args.intermediate_size % TP:
        parser().error("intermediate-size must be divisible by TP=8")
    if args.cached_tokens <= FLASH_TILE:
        parser().error("cached-tokens must exceed 8192 to select FlashAttention")

    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")

    comm_threshold_raw = os.environ.get("XLITE_COMM_OPTIMIZE_LEN")

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
        f"native_model_forward=true layers={args.layers} "
        f"nominal_allreduces_per_forward={2 * args.layers} decode_batch={args.batch} "
        f"hidden_size={args.hidden_size} intermediate_size={args.intermediate_size} "
        f"dtype={args.dtype} "
        f"cached_tokens={args.cached_tokens} comm_bytes={tensor_bytes} "
        f"expected_xccl_aiv_launch={expected_ar_aiv} "
        f"expected_fa_launch=20AIC+40AIV "
        f"ports=A:{port_a},B:{port_b}"
    )
    log(
        "FUSION_CONFIG: "
        f"XLITE_COMM_OPTIMIZE_LEN={comm_threshold_raw or '6144(default)'} "
        "all_ranks_submit_complete_forward=true "
        f"VLLM_ASCEND_ENABLE_MATMUL_ALLREDUCE="
        f"{os.environ.get('VLLM_ASCEND_ENABLE_MATMUL_ALLREDUCE', '<unset>')}"
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
            if not complete and not collector.errors():
                log(f"CONTROL: confirming apparent hang for {args.confirm_timeout}s")
                complete = collector.until("DONE", WORKERS, args.confirm_timeout)
            collector.print_states()
            if collector.errors():
                log("RESULT=NORMAL_CONTROL_RUNTIME_FAILED exit_code=2")
                return 2
            if complete:
                log("RESULT=NORMAL_CONTROL_PASSED exit_code=0")
                return 0
            log("RESULT=NORMAL_CONTROL_HUNG exit_code=1")
            return 1

        log("CROSSED: releasing 8 first complete native Model.forward calls")
        start.set()
        if not collector.until("MODEL_SUBMITTED_FIRST", TP, args.hang_timeout):
            collector.print_states()
            log("RESULT=FIRST_WAVE_FAILED exit_code=2")
            return 2
        time.sleep(args.stagger_seconds)
        log("CROSSED: releasing 8 opposite-model complete native Model.forward calls")
        start_compute.set()
        complete = collector.until("DONE", WORKERS, args.hang_timeout)
        if not complete and not collector.errors():
            log(f"CROSSED: confirming stable hang for {args.confirm_timeout}s")
            complete = collector.until("DONE", WORKERS, args.confirm_timeout)
        collector.print_states()
        if collector.errors():
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if complete:
            log("RESULT=NO_DEADLOCK exit_code=1")
            return 1
        entered = collector.count("MODEL_ENTER_SECOND")
        first_submitted = collector.count("MODEL_SUBMITTED_FIRST")
        second_submitted = collector.count("MODEL_SUBMITTED_SECOND")
        model_done = collector.count("MODEL_DONE")
        log(
            f"HANG: done={collector.count('DONE')}/16 "
            f"first_submitted={first_submitted}/8 second_enter={entered}/8 "
            f"second_submitted={second_submitted}/8 model_done={model_done}/16"
        )
        if first_submitted == TP and entered == TP and model_done == 0:
            log("RESULT=DEADLOCK_REPRODUCED exit_code=0")
            return 0
        log("RESULT=HUNG_AFTER_PROGRESS_INCONCLUSIVE exit_code=2")
        return 2
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
