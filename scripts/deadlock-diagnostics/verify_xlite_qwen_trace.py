#!/usr/bin/env python3
"""Create a minimal, intentional Qwen3/xLite capture window on one TP group.

All ranks complete one native one-layer warm-up.  On the diagnostic forward
only rank 0 submits work; the other ranks deliberately withhold their calls.
Rank 0 therefore reaches an incomplete TP collective and remains available for
the vCANN deadlock collector.  This is a diagnostics check, not a deadlock
reproducer or a correctness benchmark.
"""

from __future__ import annotations

import argparse
import importlib.util
import multiprocessing as mp
import os
from pathlib import Path
import queue
import random
import time
import traceback
from typing import Any


DEFAULT_TP = 8


def load_reproducer() -> Any:
    path = Path(__file__).parents[1] / "repro-tp8-xlite-xccl-deadlock.py"
    spec = importlib.util.spec_from_file_location("_xlite_qwen_reproducer", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def worker(
    rank: int,
    port: int,
    args: argparse.Namespace,
    warmup: Any,
    hold: Any,
    messages: Any,
) -> None:
    try:
        os.environ.update(
            XLITE_NODE_IPS="127.0.0.1",
            XLITE_PORT=str(port),
            XLITE_DISABLE_XCCL="false",
            XLITE_FLASH_ATTENTION_ENABLE="true",
        )
        import torch
        import torch_npu  # noqa: F401
        from xlite._C import AttnMHA, AttnMeta, Model, ModelConfig, Runtime

        repro = load_reproducer()
        torch.npu.set_device(rank)
        runtime = Runtime(rank, 0, rank, args.tp_size, 1)
        dtype = torch.bfloat16
        forward_args = repro.make_model_forward(
            torch,
            rank,
            runtime,
            args.batch,
            args.cached_tokens,
            dtype,
            1,
            repro.QWEN3_HEADS * repro.HEAD_DIM,
            repro.QWEN3_INTERMEDIATE,
            Model,
            ModelConfig,
            AttnMeta,
            AttnMHA,
            args.tp_size,
        )
        torch.npu.synchronize()
        messages.put(("READY", rank, os.getpid(), ""))

        warmup.wait()
        forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
        torch.npu.synchronize()
        messages.put(("WARM", rank, os.getpid(), ""))

        hold.wait()
        if rank == 0:
            messages.put(("ENTER", rank, os.getpid(), ""))
            forward_args[0].forward_with_inputs_embeds(*forward_args[1:])
            messages.put(("SUBMITTED", rank, os.getpid(), ""))
            torch.npu.synchronize()
            messages.put(("UNEXPECTED_RETURN", rank, os.getpid(), ""))
        else:
            time.sleep(args.hold_seconds + 60)
    except BaseException:
        messages.put(("ERROR", rank, os.getpid(), traceback.format_exc()))


def wait_for(messages: Any, kind: str, count: int, timeout: float) -> list[tuple[Any, ...]]:
    deadline = time.monotonic() + timeout
    matched: list[tuple[Any, ...]] = []
    while len(matched) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {kind}: {len(matched)}/{count}")
        try:
            message = messages.get(timeout=remaining)
        except queue.Empty as error:
            raise TimeoutError(f"timed out waiting for {kind}: {len(matched)}/{count}") from error
        if message[0] == "ERROR":
            raise RuntimeError(f"rank {message[1]} failed:\n{message[3]}")
        if message[0] == "UNEXPECTED_RETURN":
            raise RuntimeError("rank 0 returned from the intentionally incomplete TP forward")
        if message[0] == kind:
            matched.append(message)
    return matched


def preserve_window(messages: Any, duration: float) -> None:
    deadline = time.monotonic() + duration
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        try:
            message = messages.get(timeout=remaining)
        except queue.Empty:
            return
        if message[0] == "ERROR":
            raise RuntimeError(f"rank {message[1]} failed:\n{message[3]}")
        if message[0] == "UNEXPECTED_RETURN":
            raise RuntimeError("rank 0 returned from the intentionally incomplete TP forward")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--cached-tokens", type=int, default=9216)
    parser.add_argument("--setup-timeout", type=float, default=300)
    parser.add_argument("--hold-seconds", type=float, default=600)
    parser.add_argument("--tp-size", type=int, default=DEFAULT_TP)
    args = parser.parse_args()
    if args.batch <= 0 or args.cached_tokens <= 0 or args.tp_size <= 0:
        parser.error("batch, cached-tokens and tp-size must be positive")
    if args.setup_timeout <= 0 or args.hold_seconds <= 0:
        parser.error("timeouts must be positive")
    return args


def main() -> int:
    args = parse_args()
    if os.name != "posix":
        raise SystemExit("this hardware verifier requires Linux")
    if os.environ.get("ENPU_DEADLOCK_TRACE", "").lower() not in {"1", "true", "yes", "on"}:
        raise SystemExit("set ENPU_DEADLOCK_TRACE=1 before starting this verifier")

    context = mp.get_context("spawn")
    messages = context.Queue()
    warmup = context.Event()
    hold = context.Event()
    port = random.randint(20000, 39000)
    workers = [
        context.Process(
            target=worker,
            args=(rank, port, args, warmup, hold, messages),
            name=f"trace-verify-rank-{rank}",
        )
        for rank in range(args.tp_size)
    ]
    for process in workers:
        process.start()

    try:
        ready = wait_for(messages, "READY", args.tp_size, args.setup_timeout)
        warmup.set()
        wait_for(messages, "WARM", args.tp_size, args.setup_timeout)
        hold.set()
        entered = wait_for(messages, "ENTER", 1, 30)
        time.sleep(5)
        pids = ",".join(str(message[2]) for message in sorted(ready, key=lambda item: item[1]))
        print(
            "CAPTURE_READY "
            f"rank0_pid={entered[0][2]} all_pids={pids} hold_seconds={args.hold_seconds:g}",
            flush=True,
        )
        print(
            "Run collect_vcann_deadlock.py capture with "
            f"--expected-processes {args.tp_size}; "
            "the verifier will preserve this intentional window until the hold expires.",
            flush=True,
        )
        preserve_window(messages, args.hold_seconds)
        print("CAPTURE_WINDOW_COMPLETE", flush=True)
        return 0
    except TimeoutError as error:
        print(f"SETUP_FAILED: {error}", flush=True)
        return 2
    except (KeyboardInterrupt, RuntimeError) as error:
        print(f"STOPPED: {error}", flush=True)
        return 2
    finally:
        for process in workers:
            if process.is_alive():
                process.terminate()
        for process in workers:
            process.join(5)


if __name__ == "__main__":
    raise SystemExit(main())
