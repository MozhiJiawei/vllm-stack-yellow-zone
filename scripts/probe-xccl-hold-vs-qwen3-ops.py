#!/usr/bin/env python3
"""Probe Qwen3-32B B-side ops while model A waits in xLite AllReduce.

The script needs one host with eight Ascend NPUs.  It creates two independent
xLite TP8 runtimes on the same devices and reuses them for the whole matrix.
Model A deliberately withholds ranks 4-7 from an AllReduce; after ranks 0-3
are confirmed waiting, all model-B ranks execute warmed-up operations serially.

No checkpoint is loaded.  Tensor shapes match Qwen3-32B TP8.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import multiprocessing as mp
import os
import platform
import queue
import random
import socket
import time
import traceback
from dataclasses import dataclass

TP = 8
BATCH = 16
HIDDEN = 5120
HEADS = 64
KV_HEADS = 8
HEAD_DIM = 128
LOCAL_HEADS = HEADS // TP
LOCAL_KV_HEADS = max(1, KV_HEADS // TP)
LOCAL_ATTN = LOCAL_HEADS * HEAD_DIM
LOCAL_QKV = (LOCAL_HEADS + 2 * LOCAL_KV_HEADS) * HEAD_DIM
INTERMEDIATE = 25600
LOCAL_INTERMEDIATE = INTERMEDIATE // TP
VOCAB = 151936
LOCAL_VOCAB = VOCAB // TP
BLOCK_SIZE = 128
EPS = 1e-6
PREFILL_ROWS = (2048, 6143, 6144, 6145, 8192, 40960)


@dataclass(frozen=True)
class Case:
    name: str
    suite: str
    op: str
    rows: int = BATCH
    variant: str = ""
    cached: int = 0


def make_cases() -> list[Case]:
    cases = [
        Case("decode.embed_local", "bf16-decode", "embed"),
        Case("decode.rmsnorm_hidden", "bf16-decode", "rms_hidden"),
        Case("decode.rmsnorm_q_strided", "bf16-decode", "rms_q"),
        Case("decode.rmsnorm_k_offset", "bf16-decode", "rms_k"),
        Case("decode.matmul_qkv", "bf16-decode", "matmul", variant="qkv"),
        Case("decode.rope_cache", "bf16-decode", "rope"),
        Case("decode.attention_cached4096", "attention", "attention", cached=4096),
        Case("decode.flash_attention_cached9216", "attention", "attention", cached=9216),
        Case("decode.matmul_o", "bf16-decode", "matmul", variant="o"),
        Case("decode.add_rmsnorm_inplace", "bf16-decode", "add_rms", variant="inplace"),
        Case("decode.matmul_gate_up", "bf16-decode", "matmul", variant="gate_up"),
        Case("decode.silu_mul", "bf16-decode", "silu"),
        Case("decode.matmul_down", "bf16-decode", "matmul", variant="down"),
        Case("decode.add_rmsnorm_out", "bf16-decode", "add_rms", variant="out"),
    ]

    for rows in PREFILL_ROWS:
        for op, variant in (
            ("embed", ""),
            ("rms_hidden", ""),
            ("rms_q", ""),
            ("rms_k", ""),
            ("matmul", "qkv"),
            ("rope", ""),
            ("matmul", "o"),
            ("add_rms", "inplace"),
            ("matmul", "gate_up"),
            ("silu", ""),
            ("matmul", "down"),
        ):
            suffix = f"_{variant}" if variant else ""
            cases.append(Case(f"prefill.m{rows}.{op}{suffix}", "bf16-prefill", op, rows, variant))
        cases.append(Case(f"prefill.m{rows}.attention_first_chunk", "attention", "attention", rows))

    for cached in (4096, 9216):
        cases.append(
            Case(
                f"prefill.m6144.attention_continuation_cached{cached}",
                "attention",
                "attention",
                6144,
                "continuation",
                cached,
            )
        )
    for rows in (6144, 6145, 8192):
        cases.append(Case(f"mixed.m{rows}.attention_8d8p", "attention", "attention", rows, "mixed", 4096))

    for rows in (BATCH, 2048, 6143):
        cases.append(Case(f"collective.all_reduce_m{rows}", "collective", "all_reduce", rows))
    for rows in (6144, 6145, 8192, 40960):
        cases.append(Case(f"collective.reduce_scatter_m{rows}", "collective", "reduce_scatter", rows))
        cases.append(Case(f"collective.all_gather_m{rows}", "collective", "all_gather", rows))

    cases.extend(
        [
            Case("w8a8.quant_static", "w8a8", "quant_static"),
            Case("w8a8.quant_dynamic", "w8a8", "quant_dynamic"),
            Case("w8a8.dequant", "w8a8", "dequant"),
        ]
    )
    for projection in ("qkv", "o", "gate_up", "down"):
        for layout in ("nd", "transposed", "nz", "nz_transposed"):
            cases.append(
                Case(
                    f"w8a8.matmul_dequant_{projection}_{layout}",
                    "w8a8",
                    "matmul_dequant",
                    variant=f"{projection}:{layout}",
                )
            )

    cases.extend(
        [
            Case("postprocess.lm_head_matmul", "postprocess", "matmul", variant="lm_head"),
            Case("postprocess.vocab_all_gather", "postprocess", "vocab_all_gather"),
            Case("postprocess.greedy_argmax", "postprocess", "greedy"),
            Case("postprocess.temperature", "postprocess", "temperature"),
            Case("postprocess.penalties", "postprocess", "penalties"),
            Case("postprocess.top_k", "postprocess", "top_k"),
            Case("postprocess.top_p", "postprocess", "top_p"),
            Case("postprocess.top_k_top_p", "postprocess", "top_k_top_p"),
            Case("postprocess.softmax", "postprocess", "softmax"),
            Case("postprocess.random_exponential", "postprocess", "random_sample"),
            Case("postprocess.logprobs", "postprocess", "logprobs"),
        ]
    )

    for rows in (2048, 6144):
        for op in ("rms_hidden", "qkv", "o", "add_rms", "gate_up", "silu", "down", "softmax"):
            cases.append(Case(f"fallback.m{rows}.torch_{op}", "fallback", "torch", rows, op))

    names = [case.name for case in cases]
    if len(names) != len(set(names)):
        raise AssertionError("duplicate case name")
    return cases


CASES = make_cases()
CASE_BY_NAME = {case.name: case for case in CASES}


def projection_shape(name: str) -> tuple[int, int]:
    """Return (input K, output N) for a TP8 projection."""
    return {
        "qkv": (HIDDEN, LOCAL_QKV),
        "o": (LOCAL_ATTN, HIDDEN),
        "gate_up": (HIDDEN, LOCAL_INTERMEDIATE * 2),
        "down": (LOCAL_INTERMEDIATE, HIDDEN),
        "lm_head": (HIDDEN, LOCAL_VOCAB),
    }[name]


def distribute(total: int, count: int) -> list[int]:
    base, extra = divmod(total, count)
    return [base + (index < extra) for index in range(count)]


def padded_rows(rows: int) -> int:
    return (rows + TP - 1) // TP * TP


def configure_xlite(port: int) -> None:
    os.environ.update(
        XLITE_NODE_IPS="127.0.0.1",
        XLITE_PORT=str(port),
        XLITE_DISABLE_XCCL="false",
    )
    os.environ.pop("HCCL_DETERMINISTIC", None)


def package_version(name: str) -> str:
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return "unknown"


def build_attention(torch, xc, runtime, case: Case, device: str):
    import numpy as np

    if case.variant == "mixed":
        prefill_rows = case.rows - 8
        query_lens = [1] * 8 + distribute(prefill_rows, 8)
        cached_lens = [case.cached] * 8 + [0] * 8
    else:
        query_lens = distribute(case.rows, BATCH)
        cached_lens = [case.cached] * BATCH

    max_blocks = max(
        (query + cached + BLOCK_SIZE - 1) // BLOCK_SIZE
        for query, cached in zip(query_lens, cached_lens)
    )
    qkv = torch.randn(case.rows, LOCAL_QKV, dtype=torch.bfloat16, device=device)
    k_cache = torch.randn(
        BATCH * max_blocks, BLOCK_SIZE, LOCAL_KV_HEADS, HEAD_DIM, dtype=torch.bfloat16, device=device
    )
    v_cache = torch.randn_like(k_cache)
    output = torch.empty(case.rows, LOCAL_ATTN, dtype=torch.bfloat16, device=device)
    starts = np.cumsum(np.asarray(query_lens, dtype=np.int32)) - np.asarray(query_lens, dtype=np.int32)
    query_start = torch.tensor(starts.tolist(), dtype=torch.int32, device=device)
    query = torch.tensor(query_lens, dtype=torch.int32, device=device)
    cached = torch.tensor(cached_lens, dtype=torch.int32, device=device)
    tables = torch.arange(BATCH * max_blocks, dtype=torch.int32, device=device)
    enable_flash = max_blocks * BLOCK_SIZE > 8192

    def run():
        xc.attention(
            runtime,
            qkv,
            k_cache,
            v_cache,
            output,
            query_start,
            query,
            cached,
            tables,
            LOCAL_HEADS,
            LOCAL_KV_HEADS,
            HEAD_DIM,
            BLOCK_SIZE,
            BATCH,
            max_blocks,
            enable_flash,
        )

    return run


def build_torch_fallback(torch, case: Case, device: str):
    rows, op = case.rows, case.variant
    if op == "rms_hidden":
        x = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        weight = torch.randn(HIDDEN, dtype=torch.bfloat16, device=device)
        return lambda: torch.nn.functional.rms_norm(x, (HIDDEN,), weight, EPS)
    if op in ("qkv", "o", "gate_up", "down"):
        k, n = projection_shape(op)
        x = torch.randn(rows, k, dtype=torch.bfloat16, device=device)
        weight = torch.randn(n, k, dtype=torch.bfloat16, device=device)
        return lambda: torch.nn.functional.linear(x, weight)
    if op == "silu":
        x = torch.randn(rows, LOCAL_INTERMEDIATE * 2, dtype=torch.bfloat16, device=device)
        return lambda: torch.nn.functional.silu(x[:, :LOCAL_INTERMEDIATE]) * x[:, LOCAL_INTERMEDIATE:]
    if op == "add_rms":
        x = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        residual = torch.randn_like(x)
        weight = torch.randn(HIDDEN, dtype=torch.bfloat16, device=device)
        return lambda: torch.nn.functional.rms_norm(x + residual, (HIDDEN,), weight, EPS)
    if op == "softmax":
        x = torch.randn(rows, LOCAL_HEADS, dtype=torch.float32, device=device)
        return lambda: torch.softmax(x, dim=-1)
    raise ValueError(f"unknown torch fallback op: {op}")


def build_operation(torch, torch_npu, xc, runtime, case: Case, rank: int):
    device = f"npu:{rank}"
    rows = case.rows

    if case.op == "embed":
        weight = torch.randn(LOCAL_VOCAB, HIDDEN, dtype=torch.bfloat16, device=device)
        token_ids = rank * LOCAL_VOCAB + torch.arange(rows, dtype=torch.int32, device=device) % LOCAL_VOCAB
        output = torch.empty(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        return lambda: xc.embed(runtime, weight, token_ids, output, rank * LOCAL_VOCAB, (rank + 1) * LOCAL_VOCAB)

    if case.op == "rms_hidden":
        x = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        weight = torch.randn(HIDDEN, dtype=torch.bfloat16, device=device)
        output = torch.empty_like(x)
        return lambda: xc.rmsnorm(runtime, x, weight, output, EPS)

    if case.op in ("rms_q", "rms_k"):
        qkv = torch.randn(rows, LOCAL_QKV, dtype=torch.bfloat16, device=device)
        weight = torch.randn(HEAD_DIM, dtype=torch.bfloat16, device=device)
        if case.op == "rms_q":
            return lambda: xc.rmsnorm(runtime, qkv, weight, qkv, EPS, HEAD_DIM, LOCAL_HEADS)
        q_offset = LOCAL_HEADS * HEAD_DIM
        return lambda: xc.rmsnorm(runtime, qkv, weight, qkv, EPS, HEAD_DIM, 1, q_offset, q_offset)

    if case.op == "matmul":
        k, n = projection_shape(case.variant)
        x = torch.randn(rows, k, dtype=torch.bfloat16, device=device)
        weight = torch.randn(n, k, dtype=torch.bfloat16, device=device)
        output = torch.empty(rows, n, dtype=torch.bfloat16, device=device)
        return lambda: xc.matmul(runtime, x, weight, output, False, False)

    if case.op == "rope":
        qkv = torch.randn(rows, LOCAL_QKV, dtype=torch.bfloat16, device=device)
        query_lens = distribute(rows, BATCH)
        positions_cpu = [position for query_len in query_lens for position in range(query_len)]
        cache_blocks = (rows + BLOCK_SIZE - 1) // BLOCK_SIZE
        cache = torch.zeros(
            cache_blocks, BLOCK_SIZE, LOCAL_KV_HEADS, HEAD_DIM, dtype=torch.bfloat16, device=device
        )
        v_cache = torch.zeros_like(cache)
        positions = torch.tensor(positions_cpu, dtype=torch.int64, device=device)
        freqs = torch.randn(max(query_lens), HEAD_DIM, dtype=torch.bfloat16, device=device)
        slots = torch.arange(rows, dtype=torch.int32, device=device)
        return lambda: xc.rope_and_cache(
            runtime,
            qkv,
            cache,
            v_cache,
            positions,
            freqs,
            slots,
            # The binding expects global head counts and divides them by the
            # Runtime TP size internally.
            HEADS,
            KV_HEADS,
            HEAD_DIM,
            HEAD_DIM,
            BLOCK_SIZE,
            True,
        )

    if case.op == "attention":
        return build_attention(torch, xc, runtime, case, device)

    if case.op == "silu":
        x = torch.randn(rows, LOCAL_INTERMEDIATE * 2, dtype=torch.bfloat16, device=device)
        output = torch.empty(rows, LOCAL_INTERMEDIATE, dtype=torch.bfloat16, device=device)
        return lambda: xc.silu_and_mul(runtime, x, output)

    if case.op == "add_rms":
        x = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        residual = torch.randn_like(x)
        weight = torch.randn(HIDDEN, dtype=torch.bfloat16, device=device)
        output = x if case.variant == "inplace" else torch.empty_like(x)
        return lambda: xc.add_and_rmsnorm(runtime, x, residual, weight, output, EPS)

    if case.op in ("all_reduce", "reduce_scatter", "all_gather"):
        collective = getattr(xc, case.op)
        padded = padded_rows(rows)
        if case.op == "all_reduce":
            source = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
            output = torch.empty_like(source)
        elif case.op == "reduce_scatter":
            source = torch.randn(padded, HIDDEN, dtype=torch.bfloat16, device=device)
            output = torch.empty(padded // TP, HIDDEN, dtype=torch.bfloat16, device=device)
        else:
            source = torch.randn(padded // TP, HIDDEN, dtype=torch.bfloat16, device=device)
            output = torch.empty(padded, HIDDEN, dtype=torch.bfloat16, device=device)
        return lambda: collective(runtime, output, source, 0)

    if case.op in ("quant_static", "quant_dynamic"):
        x = torch.randn(rows, HIDDEN, dtype=torch.bfloat16, device=device)
        output = torch.empty(rows, HIDDEN, dtype=torch.int8, device=device)
        if case.op == "quant_dynamic":
            scale = torch.empty(rows, dtype=torch.float32, device=device)
            return lambda: xc.quant_dynamic(runtime, x, scale, output)
        scale = torch.randn(HIDDEN, dtype=torch.bfloat16, device=device)
        offset = torch.zeros(HIDDEN, dtype=torch.bfloat16, device=device)
        return lambda: xc.quant(runtime, x, scale, offset, output)

    if case.op == "matmul_dequant":
        projection, layout = case.variant.split(":")
        k, n = projection_shape(projection)
        transpose = "transposed" in layout
        use_nz = layout.startswith("nz")
        x = torch.randint(-8, 8, (rows, k), dtype=torch.int8, device=device)
        weight_shape = (k, n) if transpose else (n, k)
        weight = torch.randint(-8, 8, weight_shape, dtype=torch.int8, device=device)
        if use_nz:
            torch.npu.set_option({"ALLOW_INTERNAL_FORMAT": True})
            weight = torch_npu.npu_format_cast(weight, 29)
        bias = torch.zeros(n, dtype=torch.int32, device=device)
        scale = torch.zeros(n * 2, dtype=torch.float32, device=device)
        scale[0::2] = 1.0
        output = torch.empty(rows, n, dtype=torch.float16, device=device)
        return lambda: xc.matmul_dequant(runtime, x, weight, bias, scale, output, use_nz, transpose)

    if case.op == "dequant":
        x = torch.randn(rows, HIDDEN, dtype=torch.float16, device=device)
        scale = torch.ones(rows, dtype=torch.float32, device=device)
        return lambda: xc.dequant(runtime, x, scale, x, True)

    if case.op == "vocab_all_gather":
        source = torch.randn(BATCH, LOCAL_VOCAB, dtype=torch.bfloat16, device=device)
        output = torch.empty(BATCH * TP, LOCAL_VOCAB, dtype=torch.bfloat16, device=device)
        return lambda: xc.all_gather(runtime, output, source, 0)

    if case.op in {
        "greedy",
        "temperature",
        "penalties",
        "top_k",
        "top_p",
        "top_k_top_p",
        "softmax",
        "random_sample",
        "logprobs",
    }:
        logits = torch.randn(BATCH, VOCAB, dtype=torch.bfloat16, device=device)
        if case.op == "greedy":
            return lambda: logits.argmax(dim=-1)
        if case.op == "temperature":
            temperature = torch.linspace(0.7, 1.3, BATCH, dtype=torch.float32, device=device).unsqueeze(1)
            return lambda: logits.div_(temperature)
        if case.op == "penalties":
            from vllm_ascend.ops.triton.penalty import apply_penalties_triton

            prompt = torch.randint(0, VOCAB, (BATCH, 32), dtype=torch.int64, device=device)
            output_tokens = torch.randint(0, VOCAB, (BATCH, 32), dtype=torch.int64, device=device)
            presence = torch.full((BATCH,), 0.1, dtype=torch.float32, device=device)
            frequency = torch.full((BATCH,), 0.1, dtype=torch.float32, device=device)
            repetition = torch.full((BATCH,), 1.1, dtype=torch.float32, device=device)
            return lambda: apply_penalties_triton(logits, prompt, output_tokens, presence, frequency, repetition)
        if case.op in ("top_k", "top_p", "top_k_top_p"):
            k = None if case.op == "top_p" else torch.full((BATCH,), 50, dtype=torch.int32, device=device)
            p = None if case.op == "top_k" else torch.full((BATCH,), 0.9, dtype=torch.float32, device=device)
            return lambda: torch.ops._C_ascend.npu_apply_top_k_top_p(logits, k=k, p=p)
        if case.op == "softmax":
            return lambda: torch.softmax(logits, dim=-1, dtype=torch.float32)
        if case.op == "random_sample":
            probabilities = torch.empty(BATCH, VOCAB, dtype=torch.float32, device=device)
            q = torch.empty_like(probabilities)

            def random_sample():
                q.exponential_()
                return probabilities.div(q).argmax(dim=-1)

            return random_sample
        return lambda: torch.topk(torch.log_softmax(logits, dim=-1, dtype=torch.float32), 20, dim=-1)

    if case.op == "torch":
        return build_torch_fallback(torch, case, device)

    raise ValueError(f"unknown operation: {case.op}")


def emit(messages, kind: str, role: str, rank: int, task: str = "-", detail: str = "") -> None:
    messages.put((kind, role, rank, task, detail))


def model_a(rank, port, start, called, returned, messages):
    try:
        configure_xlite(port)
        import torch
        import torch_npu  # noqa: F401
        from xlite._C import Runtime, all_reduce

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 128, rank, TP, 1)
        source = torch.ones((BATCH, HIDDEN), dtype=torch.bfloat16, device=f"npu:{rank}")
        output = torch.empty_like(source)
        torch.npu.synchronize()
        emit(messages, "READY", "A", rank)
        start.wait()
        if rank < TP // 2:
            called.set()
            emit(messages, "ENTER", "A", rank, detail="all_reduce")
            all_reduce(runtime, output, source, 0)
            torch.npu.synchronize()
            returned.set()
            emit(messages, "RETURN", "A", rank, detail="all_reduce")
        else:
            time.sleep(3600)
    except BaseException:
        emit(messages, "ERROR", "A", rank, detail=traceback.format_exc())


def release_tensors(torch) -> None:
    import gc

    gc.collect()
    torch.npu.empty_cache()


def task_key(case: Case, attempt: int) -> str:
    return f"{case.name}@{attempt}"


def model_b(
    rank,
    port,
    model_a_ready,
    cases,
    repeat,
    start,
    warmup_failed_events,
    prepare_events,
    run_events,
    messages,
):
    stage = "import"
    current_task = "-"
    try:
        configure_xlite(port)
        import torch
        import torch_npu
        import xlite._C as xc

        model_a_ready.wait()
        torch.npu.set_device(rank)
        stage = "runtime"
        runtime = xc.Runtime(rank, 4096, rank, TP, 1)
        for case_index, case in enumerate(cases):
            current_task = case.name
            stage = f"warmup:{case.name}"
            if rank == 0:
                emit(messages, "WARMUP_START", "B", rank, case.name)
            operation = None
            try:
                operation = build_operation(torch, torch_npu, xc, runtime, case, rank)
                operation()
                torch.npu.synchronize()
                if rank == 0:
                    emit(messages, "WARMUP_DONE", "B", rank, case.name)
            except BaseException:
                warmup_failed_events[case_index].set()
                emit(messages, "WARMUP_FAILED", "B", rank, case.name, traceback.format_exc())
            finally:
                del operation
                try:
                    release_tensors(torch)
                except BaseException:
                    warmup_failed_events[case_index].set()
                    emit(messages, "WARMUP_CLEANUP_FAILED", "B", rank, case.name, traceback.format_exc())
        detail = ""
        if rank == 0:
            cann = getattr(torch.version, "cann", "unknown")
            detail = (
                f"python={platform.python_version()} torch={package_version('torch')} "
                f"torch_npu={package_version('torch-npu')} xlite={package_version('xlite')} "
                f"cann={cann} device={torch.npu.get_device_name(rank)!r}"
            )
        emit(messages, "READY", "B", rank, detail=detail)
        start.wait()
        tasks = [(case, attempt) for case in cases for attempt in range(1, repeat + 1)]
        for index, (case, attempt) in enumerate(tasks):
            current_task = task_key(case, attempt)
            prepare_events[index].wait()
            if warmup_failed_events[index // repeat].is_set():
                emit(messages, "SKIPPED", "B", rank, current_task, "reason=warmup_failed")
                continue
            stage = f"prepare:{current_task}"
            operation = build_operation(torch, torch_npu, xc, runtime, case, rank)
            torch.npu.synchronize()
            emit(messages, "PREPARED", "B", rank, current_task)
            run_events[index].wait()
            emit(messages, "ENTER", "B", rank, current_task)
            stage = f"test:{current_task}"
            begin = time.monotonic()
            operation()
            torch.npu.synchronize()
            emit(messages, "DONE", "B", rank, current_task, f"elapsed={time.monotonic() - begin:.6f}")
            del operation
            release_tensors(torch)
    except BaseException:
        emit(messages, "ERROR", "B", rank, current_task, f"stage={stage}\n{traceback.format_exc()}")


def find_ports() -> tuple[int, int]:
    for _ in range(200):
        ports = random.sample(range(20000, 50000), 2)
        expanded = [port for base in ports for port in (base, base + 400)]
        if len(set(expanded)) != 4:
            continue
        sockets = []
        try:
            for port in expanded:
                sock = socket.socket()
                sock.bind(("127.0.0.1", port))
                sockets.append(sock)
            return ports[0], ports[1]
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("cannot find two free xLite port pairs")


def show_message(kind: str, role: str, rank: int, task: str, detail: str) -> None:
    suffix = f" {detail}" if detail else ""
    print(f"EVENT role={role} rank={rank} state={kind} task={task}{suffix}", flush=True)


def wait_role_ready(messages, wanted_role: str, timeout: float) -> tuple[bool, bool]:
    deadline = time.monotonic() + timeout
    ready: set[int] = set()
    while len(ready) < TP:
        try:
            item = messages.get(timeout=max(0.0, deadline - time.monotonic()))
        except queue.Empty:
            print(
                f"SETUP_TIMEOUT role={wanted_role} missing_ranks={sorted(set(range(TP)) - ready)}",
                flush=True,
            )
            return False, False
        kind, role, rank, task, detail = item
        show_message(kind, role, rank, task, detail)
        if kind == "ERROR":
            return False, True
        if kind == "READY" and role == wanted_role:
            ready.add(rank)
    return True, False


def wait_events(events, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    for event in events:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not event.wait(remaining):
            return False
    return True


def wait_for_task(messages, wanted: str, task: str, timeout: float) -> tuple[set[int], bool]:
    deadline = time.monotonic() + timeout
    seen: set[int] = set()
    failed = False
    while len(seen) < TP and not failed:
        try:
            kind, role, rank, event_task, detail = messages.get(timeout=max(0.0, deadline - time.monotonic()))
        except queue.Empty:
            break
        show_message(kind, role, rank, event_task, detail)
        if kind == "ERROR":
            failed = True
        elif role == "B" and kind == wanted and event_task == task:
            seen.add(rank)
    return seen, failed


def mark_not_run(tasks, results):
    for case, attempt in tasks[len(results) :]:
        results.append((case.name, attempt, "NOT_RUN"))
    return results


def run_matrix(cases: list[Case], repeat: int, timeout: float, setup_timeout: float, init_stagger: float):
    ctx = mp.get_context("spawn")
    messages = ctx.Queue()
    model_a_ready = ctx.Event()
    a_start, b_start, returned = ctx.Event(), ctx.Event(), ctx.Event()
    called = [ctx.Event() for _ in range(TP)]
    tasks = [(case, attempt) for case in cases for attempt in range(1, repeat + 1)]
    warmup_failed_events = [ctx.Event() for _ in cases]
    prepare_events = [ctx.Event() for _ in tasks]
    run_events = [ctx.Event() for _ in tasks]
    port_a, port_b = find_ports()
    jobs = [
        ctx.Process(target=model_a, args=(rank, port_a, a_start, called[rank], returned, messages))
        for rank in range(TP)
    ]
    jobs.extend(
        ctx.Process(
            target=model_b,
            args=(
                rank,
                port_b,
                model_a_ready,
                cases,
                repeat,
                b_start,
                warmup_failed_events,
                prepare_events,
                run_events,
                messages,
            ),
        )
        for rank in range(TP)
    )
    print(
        f"MATRIX_START cases={len(cases)} tasks={len(tasks)} port_a={port_a} port_b={port_b}",
        flush=True,
    )
    for job in jobs:
        job.start()
    try:
        results: list[tuple[str, int, str]] = []
        print("INIT_PHASE model=A", flush=True)
        ready, failed = wait_role_ready(messages, "A", setup_timeout)
        if failed or not ready:
            if tasks:
                case, attempt = tasks[0]
                results.append((case.name, attempt, "SETUP_FAILED"))
            return mark_not_run(tasks, results)
        print(f"INIT_STAGGER seconds={init_stagger:g}", flush=True)
        time.sleep(init_stagger)
        model_a_ready.set()
        print("INIT_PHASE model=B", flush=True)
        ready, failed = wait_role_ready(messages, "B", setup_timeout)
        if failed or not ready:
            case, attempt = tasks[0]
            results.append((case.name, attempt, "SETUP_FAILED"))
            return mark_not_run(tasks, results)
        a_start.set()
        if not wait_events(called[: TP // 2], timeout):
            case, attempt = tasks[0]
            return mark_not_run(tasks, [(case.name, attempt, "SETUP_FAILED")])
        time.sleep(3)
        if returned.is_set():
            print("INVALID A AllReduce returned before B started", flush=True)
            case, attempt = tasks[0]
            return mark_not_run(tasks, [(case.name, attempt, "SETUP_FAILED")])
        print("A_BLOCKED ranks=0,1,2,3 waited_seconds=3", flush=True)
        b_start.set()
        for index, (case, attempt) in enumerate(tasks):
            key = task_key(case, attempt)
            print(
                f"CASE_START name={case.name} attempt={attempt} suite={case.suite} rows={case.rows} "
                f"variant={case.variant or '-'} cached={case.cached}",
                flush=True,
            )
            prepare_events[index].set()
            if warmup_failed_events[index // repeat].is_set():
                skipped, failed = wait_for_task(messages, "SKIPPED", key, setup_timeout)
                if failed or len(skipped) != TP:
                    missing = sorted(set(range(TP)) - skipped)
                    print(f"SKIP_FAILED task={key} missing_ranks={missing}", flush=True)
                    results.append((case.name, attempt, "SETUP_FAILED"))
                    break
                results.append((case.name, attempt, "SKIPPED"))
                print(
                    f"CASE_RESULT name={case.name} attempt={attempt} "
                    "status=SKIPPED reason=warmup_failed",
                    flush=True,
                )
                continue
            prepared, failed = wait_for_task(messages, "PREPARED", key, setup_timeout)
            if failed or len(prepared) != TP:
                missing = sorted(set(range(TP)) - prepared)
                print(f"PREPARE_FAILED task={key} missing_ranks={missing}", flush=True)
                results.append((case.name, attempt, "SETUP_FAILED"))
                break
            run_events[index].set()
            done, failed = wait_for_task(messages, "DONE", key, timeout)
            if failed:
                status = "SETUP_FAILED"
            elif returned.is_set():
                print("INVALID A AllReduce returned during B operation", flush=True)
                status = "SETUP_FAILED"
            elif len(done) != TP:
                print(f"B_TIMEOUT task={key} blocked_ranks={sorted(set(range(TP)) - done)}", flush=True)
                status = "BLOCKED"
            else:
                status = "PASS"
            results.append((case.name, attempt, status))
            print(f"CASE_RESULT name={case.name} attempt={attempt} status={status}", flush=True)
            if status != "PASS":
                break
        return mark_not_run(tasks, results)
    finally:
        for job in jobs:
            if job.is_alive():
                job.terminate()
        for job in jobs:
            job.join(5)
        for job in jobs:
            if job.is_alive():
                print(f"PROCESS_FORCE_KILL name={job.name} pid={job.pid}", flush=True)
                job.kill()
        for job in jobs:
            job.join(5)
        messages.close()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list cases without importing torch/xLite")
    parser.add_argument("--suite", default="all", help="suite name, or all (default: all)")
    parser.add_argument("--case", action="append", dest="case_names", help="exact case name; repeatable")
    parser.add_argument("--timeout", type=float, default=30.0, help="A entry and B completion timeout")
    parser.add_argument("--setup-timeout", type=float, default=3600.0, help="matrix warmup/per-case prepare timeout")
    parser.add_argument(
        "--init-stagger-seconds",
        type=float,
        default=10.0,
        help="wait after all A ranks initialize before allowing B Runtime initialization",
    )
    parser.add_argument("--repeat", type=int, default=1, help="repeat every selected case")
    parser.add_argument(
        "--batch-size",
        type=int,
        default=10,
        help="cases per fresh A/B process group and Runtime initialization (default: 10)",
    )
    args = parser.parse_args()
    if (
        args.timeout <= 0
        or args.setup_timeout <= 0
        or args.repeat <= 0
        or args.batch_size <= 0
        or args.init_stagger_seconds < 0
    ):
        parser.error("timeouts/repeat/batch size must be positive and init stagger must be non-negative")
    return args


def select_cases(args) -> list[Case]:
    if args.case_names:
        unknown = sorted(set(args.case_names) - CASE_BY_NAME.keys())
        if unknown:
            raise SystemExit(f"unknown case(s): {', '.join(unknown)}")
        return [CASE_BY_NAME[name] for name in args.case_names]
    suites = {case.suite for case in CASES}
    if args.suite != "all" and args.suite not in suites:
        raise SystemExit(f"unknown suite {args.suite!r}; choose from: {', '.join(sorted(suites))}")
    return [case for case in CASES if args.suite == "all" or case.suite == args.suite]


def main() -> int:
    args = parse_args()
    if args.list:
        for case in CASES:
            pad = padded_rows(case.rows) if case.op in ("all_gather", "reduce_scatter") else case.rows
            print(
                f"{case.name}\tsuite={case.suite}\top={case.op}\trows={case.rows}"
                f"\tpadded_rows={pad}\tvariant={case.variant or '-'}\tcached={case.cached}"
            )
        print(f"TOTAL={len(CASES)}")
        return 0

    selected = select_cases(args)
    print(
        f"ENV host={platform.node()} python={platform.python_version()} "
        f"tp={TP} batch={BATCH} hidden={HIDDEN} intermediate={INTERMEDIATE} "
        f"heads={HEADS} kv_heads={KV_HEADS} head_dim={HEAD_DIM} vocab={VOCAB}",
        flush=True,
    )
    results = []
    batch_count = (len(selected) + args.batch_size - 1) // args.batch_size
    for batch_index, offset in enumerate(range(0, len(selected), args.batch_size), 1):
        batch = selected[offset : offset + args.batch_size]
        print(
            f"PROBE_BATCH_START index={batch_index}/{batch_count} cases={len(batch)} "
            f"first={batch[0].name} last={batch[-1].name}",
            flush=True,
        )
        try:
            batch_results = run_matrix(
                batch,
                args.repeat,
                args.timeout,
                args.setup_timeout,
                args.init_stagger_seconds,
            )
        except Exception:
            print(f"PROBE_BATCH_ERROR index={batch_index}\n{traceback.format_exc()}", flush=True)
            batch_tasks = [(case, attempt) for case in batch for attempt in range(1, args.repeat + 1)]
            first_case, first_attempt = batch_tasks[0]
            batch_results = mark_not_run(
                batch_tasks,
                [(first_case.name, first_attempt, "SETUP_FAILED")],
            )
        results.extend(batch_results)
        print(f"PROBE_BATCH_DONE index={batch_index}/{batch_count}", flush=True)

    print("FINAL_SUMMARY", flush=True)
    for name, attempt, status in results:
        print(f"SUMMARY name={name} attempt={attempt} status={status}", flush=True)
    counts = {
        status: sum(result == status for _, _, result in results)
        for status in ("PASS", "BLOCKED", "SKIPPED", "SETUP_FAILED", "NOT_RUN")
    }
    print("COUNTS " + " ".join(f"{key}={value}" for key, value in counts.items()), flush=True)
    if counts["SETUP_FAILED"] or counts["SKIPPED"]:
        return 2
    if counts["BLOCKED"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
