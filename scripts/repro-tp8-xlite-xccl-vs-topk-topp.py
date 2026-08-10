#!/usr/bin/env python3
"""Probe whether one incomplete TP8 XCCL AllReduce blocks Sampling sort.

This is the minimal A/B operator footprint from issue #15:

* model A creates an xLite TP8 runtime on NPUs 0..7, but only rank 0 submits
  an AllReduce.  Its BF16 input has 4 * 5120 = 20480 elements.
* model B runs ``npu_apply_top_k_top_p`` on the same physical NPU as A rank 0.
  Its FP32 logits have shape [4, 151936], which selects the reported
  ``aclnnApplyTopKTopPCustom_SortAiCore_Sort`` path.

Model B is warmed up before A is blocked.  The measured invocation starts only
after A has entered the incomplete collective.  No checkpoint or vCANN is
needed; run this inside the same Linux Ascend container where
``repro-tp8-xlite-xccl-deadlock.py`` works.
"""

from __future__ import annotations

import argparse
import importlib.util
import multiprocessing as mp
import os
from pathlib import Path
import queue
import random
import socket
import sys
import time
import traceback
from typing import Any


TP = 8
ISSUE_BATCH = 4
HIDDEN_SIZE = 5120
VOCAB_SIZE = 151936
ALLREDUCE_COUNT = ISSUE_BATCH * HIDDEN_SIZE


def register_bundled_custom_opp() -> None:
    """Expose vLLM Ascend's packaged kernels before torch initializes CANN."""
    spec = importlib.util.find_spec("vllm_ascend")
    if spec is None or not spec.submodule_search_locations:
        raise RuntimeError("cannot locate the installed vllm_ascend package")

    package_root = Path(next(iter(spec.submodule_search_locations)))
    custom_opp = package_root / "_cann_ops_custom" / "vendors" / "vllm-ascend"
    if not custom_opp.is_dir():
        raise RuntimeError(f"vLLM Ascend custom OPP directory is missing: {custom_opp}")

    entries = [
        item
        for item in os.environ.get("ASCEND_CUSTOM_OPP_PATH", "").split(":")
        if item
    ]
    custom_opp_text = str(custom_opp)
    if custom_opp_text not in entries:
        os.environ["ASCEND_CUSTOM_OPP_PATH"] = ":".join([custom_opp_text, *entries])


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def free_xlite_port() -> int:
    """Find a free xLite base port; xLite also reserves base + 400."""
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


def emit(messages: Any, kind: str, role: str, rank: int, detail: str = "") -> None:
    messages.put((kind, role, rank, detail))


def model_a(
    rank: int,
    port: int,
    start: Any,
    entered: Any,
    stop_requested: Any,
    messages: Any,
) -> None:
    label = f"model=A rank={rank} npu={rank}"
    try:
        os.environ.update(
            XLITE_NODE_IPS="127.0.0.1",
            XLITE_PORT=str(port),
            XLITE_DISABLE_XCCL="false",
        )
        import torch
        import torch_npu  # noqa: F401 - registers the NPU backend
        from xlite._C import Runtime, all_reduce

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 128, rank, TP, 1)
        source = torch.ones(
            (ISSUE_BATCH, HIDDEN_SIZE),
            dtype=torch.bfloat16,
            device=f"npu:{rank}",
        )
        output = torch.empty_like(source)
        torch.npu.synchronize()
        emit(messages, "READY", "A", rank, label)

        start.wait()
        if rank != 0:
            # Keep the other seven TP ranks alive without submitting the
            # collective.  This is the intentional missing-rank condition.
            stop_requested.wait()
            return

        entered.set()
        emit(messages, "ENTER", "A", rank, "all_reduce count=20480 dtype=bf16")
        all_reduce(runtime, output, source, 0)
        emit(messages, "SUBMITTED", "A", rank, "host call returned")
        torch.npu.synchronize()
        emit(messages, "DONE", "A", rank, "unexpected collective completion")
    except BaseException:
        emit(messages, "ERROR", "A", rank, traceback.format_exc())


def model_b(
    device: int,
    start: Any,
    entered: Any,
    messages: Any,
    top_k: int,
    top_p: float,
) -> None:
    try:
        register_bundled_custom_opp()
        import torch
        import torch_npu  # noqa: F401 - registers the NPU backend
        from vllm_ascend.utils import enable_custom_op

        torch.npu.set_device(device)
        if not enable_custom_op():
            raise RuntimeError("vLLM Ascend custom operators are disabled")

        logits = torch.empty(
            (ISSUE_BATCH, VOCAB_SIZE),
            dtype=torch.float32,
            device=f"npu:{device}",
        ).uniform_(-5.0, 5.0)
        k = torch.full(
            (ISSUE_BATCH,), top_k, dtype=torch.int32, device=f"npu:{device}"
        )
        p = torch.full(
            (ISSUE_BATCH,), top_p, dtype=torch.float32, device=f"npu:{device}"
        )

        def operation() -> Any:
            return torch.ops._C_ascend.npu_apply_top_k_top_p(logits, k=k, p=p)

        warmup_started = time.monotonic()
        warmup_output = operation()
        torch.npu.synchronize()
        warmup_elapsed = time.monotonic() - warmup_started
        if warmup_output.shape != logits.shape or warmup_output.dtype != torch.float32:
            raise RuntimeError(
                "unexpected warmup output: "
                f"shape={tuple(warmup_output.shape)} dtype={warmup_output.dtype}"
            )
        emit(
            messages,
            "READY",
            "B",
            device,
            f"warmup_elapsed={warmup_elapsed:.6f}s shape=4x151936 dtype=float32",
        )

        start.wait()
        entered.set()
        emit(messages, "ENTER", "B", device, "npu_apply_top_k_top_p")
        begin = time.monotonic()
        output = operation()
        emit(messages, "SUBMITTED", "B", device, "host call returned")
        torch.npu.synchronize()
        elapsed = time.monotonic() - begin
        if output.shape != logits.shape:
            raise RuntimeError(f"unexpected output shape: {tuple(output.shape)}")
        emit(messages, "DONE", "B", device, f"elapsed={elapsed:.6f}s")
    except BaseException:
        emit(messages, "ERROR", "B", device, traceback.format_exc())


class Events:
    def __init__(self, messages: Any) -> None:
        self.messages = messages
        self.items: list[tuple[str, str, int, str]] = []
        self.states: dict[tuple[str, int], str] = {}

    def receive(self, timeout: float) -> tuple[str, str, int, str] | None:
        try:
            item = self.messages.get(timeout=max(0.0, timeout))
        except queue.Empty:
            return None
        kind, role, rank, detail = item
        self.items.append(item)
        self.states[(role, rank)] = kind
        suffix = f" detail={detail}" if detail else ""
        log(f"EVENT role={role} rank={rank} state={kind}{suffix}")
        return item

    def count(self, kind: str, role: str | None = None) -> int:
        return sum(
            item_kind == kind and (role is None or item_role == role)
            for item_kind, item_role, _rank, _detail in self.items
        )

    def has_error(self) -> bool:
        return self.count("ERROR") > 0

    def until(self, kind: str, count: int, timeout: float, role: str | None = None) -> bool:
        deadline = time.monotonic() + timeout
        while self.count(kind, role) < count and not self.has_error():
            remaining = deadline - time.monotonic()
            if remaining <= 0 or self.receive(remaining) is None:
                break
        return self.count(kind, role) >= count

    def collect_for(self, duration: float) -> None:
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if self.receive(deadline - time.monotonic()) is None:
                return


def stop(processes: list[mp.Process], stop_requested: Any) -> None:
    stop_requested.set()
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
    result.add_argument("--device", type=int, default=0, help="shared A/B NPU (default: 0)")
    result.add_argument("--top-k", type=int, default=50)
    result.add_argument("--top-p", type=float, default=0.9)
    result.add_argument("--startup-timeout", type=float, default=600)
    result.add_argument("--hang-timeout", type=float, default=30)
    result.add_argument("--hold-confirm-seconds", type=float, default=3)
    result.add_argument("--operator-timeout", type=float, default=30)
    return result


def main() -> int:
    args = parser().parse_args()
    if sys.platform != "linux":
        log("ERROR: run this script inside the Linux Ascend container")
        log("RESULT=UNSUPPORTED_PLATFORM exit_code=2")
        return 2
    if not 0 <= args.device < TP:
        parser().error(f"device must be between 0 and {TP - 1}")
    if args.device != 0:
        parser().error("only device 0 is valid because model A submits only rank 0")
    if not 1 <= args.top_k <= VOCAB_SIZE:
        parser().error(f"top-k must be between 1 and {VOCAB_SIZE}")
    if not 0.0 < args.top_p <= 1.0:
        parser().error("top-p must be in (0, 1]")
    if any(
        value <= 0
        for value in (
            args.startup_timeout,
            args.hang_timeout,
            args.hold_confirm_seconds,
            args.operator_timeout,
        )
    ):
        parser().error("all timeouts must be positive")

    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")

    try:
        port = free_xlite_port()
    except BaseException:
        log(f"SETUP ERROR:\n{traceback.format_exc()}")
        log("RESULT=SETUP_FAILED exit_code=2")
        return 2

    log(
        "CONFIG: tp=8 A_submit_ranks=0 A_count=20480 A_dtype=bf16 "
        f"B_device={args.device} B_shape=4x151936 B_dtype=float32 "
        f"top_k={args.top_k} top_p={args.top_p:g} xlite_port={port}"
    )
    ctx = mp.get_context("spawn")
    messages = ctx.Queue()
    a_start = ctx.Event()
    b_start = ctx.Event()
    a_entered = ctx.Event()
    b_entered = ctx.Event()
    stop_requested = ctx.Event()
    processes = [
        ctx.Process(
            target=model_a,
            name=f"model-A-rank-{rank}",
            args=(rank, port, a_start, a_entered, stop_requested, messages),
        )
        for rank in range(TP)
    ]
    events = Events(messages)

    try:
        for process in processes:
            process.start()
        if not events.until("READY", TP, args.startup_timeout, role="A"):
            log("RESULT=SETUP_FAILED stage=A_init exit_code=2")
            return 2

        b_process = ctx.Process(
            target=model_b,
            name="model-B-sampling-rank-0",
            args=(
                args.device,
                b_start,
                b_entered,
                messages,
                args.top_k,
                args.top_p,
            ),
        )
        b_process.start()
        processes.append(b_process)
        if not events.until("READY", 1, args.startup_timeout, role="B"):
            log("RESULT=SETUP_FAILED stage=B_warmup exit_code=2")
            return 2

        log("START_A: rank 0 enters TP8 AllReduce; ranks 1..7 submit nothing")
        a_start.set()
        if not a_entered.wait(args.hang_timeout):
            log("RESULT=SETUP_FAILED stage=A_enter exit_code=2")
            return 2
        events.collect_for(args.hold_confirm_seconds)
        if events.count("DONE", role="A"):
            log("RESULT=ALLREDUCE_DID_NOT_BLOCK exit_code=2")
            return 2
        if events.has_error():
            log("RESULT=SETUP_FAILED stage=A_hold exit_code=2")
            return 2

        a_state = events.states.get(("A", 0), "ENTER")
        log(f"A_BLOCKED: rank=0 state={a_state}; starting B Sampling operator")
        b_start.set()
        if not b_entered.wait(args.hang_timeout):
            log("RESULT=SETUP_FAILED stage=B_enter exit_code=2")
            return 2
        completed = events.until("DONE", 1, args.operator_timeout, role="B")
        if events.has_error():
            log("RESULT=RUNTIME_FAILED exit_code=2")
            return 2
        if events.count("DONE", role="A"):
            log("RESULT=ALLREDUCE_DID_NOT_STAY_BLOCKED exit_code=2")
            return 2
        if completed:
            log("RESULT=PASS B_OPERATOR_COMPLETED_WHILE_A_BLOCKED exit_code=0")
            return 0

        b_state = events.states.get(("B", args.device), "ENTER")
        log(f"RESULT=BLOCKED_BY_A_ALLREDUCE B_state={b_state} exit_code=1")
        return 1
    finally:
        stop(processes, stop_requested)
        messages.close()
        log("CLEANUP complete")


if __name__ == "__main__":
    raise SystemExit(main())
