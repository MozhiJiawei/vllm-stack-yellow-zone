from __future__ import annotations

import argparse
import math
import multiprocessing
import os
import tempfile
import time
from array import array
from pathlib import Path
from typing import Any

from vllm_pair_scheduler import PairSchedulerConfig, SharedMemoryForwardGate


def percentile(values: list[int], quantile: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(quantile * len(ordered)) - 1)
    return ordered[index] / 1_000


def run_participant(
    role: str,
    instance: str,
    pair_id: str,
    shm_dir: str,
    iterations: int,
    warmup: int,
    barrier: Any,
    output: Any,
    cpu: int | None,
    worker_rank: int,
    worker_count: int,
) -> None:
    if cpu is not None:
        os.sched_setaffinity(0, {cpu + worker_rank})
    config = PairSchedulerConfig(
        mode="elastic",
        role=role,
        instance_id=instance,
        pair_id=pair_id,
        shm_dir=Path(shm_dir),
        init_timeout_ms=30_000,
        forward_timeout_ms=30_000,
        heartbeat_ms=10,
        peer_timeout_ms=1_000,
    )
    with SharedMemoryForwardGate(
        config, worker_rank=worker_rank, worker_count=worker_count
    ) as gate:
        barrier.wait()
        for _ in range(warmup):
            grant = gate.acquire()
            gate.complete(grant)
        barrier.wait()
        requests = array("Q")
        starts = array("Q")
        ends = array("Q")
        for _ in range(iterations):
            if worker_rank == 0:
                requests.append(time.monotonic_ns())
            grant = gate.acquire()
            if worker_rank == 0:
                starts.append(time.monotonic_ns())
                ends.append(time.monotonic_ns())
            gate.complete(grant)
        barrier.wait()
        if worker_rank == 0:
            output.put((instance, requests, starts, ends))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="File-I/O-free two-process pair-scheduler handoff benchmark"
    )
    parser.add_argument("--iterations", type=int, default=100_000)
    parser.add_argument("--warmup", type=int, default=2_000)
    parser.add_argument("--cpu-a", type=int)
    parser.add_argument("--cpu-b", type=int)
    parser.add_argument(
        "--workers",
        type=int,
        default=1,
        choices=(1, 2, 4),
        help="Local TP workers per instance; CPU options identify each rank-0 CPU",
    )
    parser.add_argument("--max-p99-us", type=float)
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=1_800,
        help="Maximum time to wait for the fixed-CPU run to finish",
    )
    args = parser.parse_args()
    if args.iterations <= 0 or args.warmup < 0 or args.timeout_seconds <= 0:
        parser.error(
            "iterations and timeout must be positive; warmup must be non-negative"
        )

    context = multiprocessing.get_context("spawn")
    barrier = context.Barrier(2 * args.workers)
    output = context.Queue()
    pair_id = f"benchmark-{os.getpid()}-{time.monotonic_ns()}"
    with tempfile.TemporaryDirectory(prefix="vllm-pair-benchmark-") as shm_dir:
        processes = []
        for role, instance, cpu in (
            ("primary", "A", args.cpu_a),
            ("standby", "B", args.cpu_b),
        ):
            for worker_rank in range(args.workers):
                processes.append(
                    context.Process(
                        target=run_participant,
                        args=(
                            role,
                            instance,
                            pair_id,
                            shm_dir,
                            args.iterations,
                            args.warmup,
                            barrier,
                            output,
                            cpu,
                            worker_rank,
                            args.workers,
                        ),
                    )
                )
        for process in processes:
            process.start()
        try:
            results = [
                output.get(timeout=args.timeout_seconds) for _ in range(2)
            ]
        except BaseException:
            for process in processes:
                if process.is_alive():
                    process.terminate()
            for process in processes:
                process.join(timeout=10)
            raise
        for process in processes:
            process.join(timeout=30)
            if process.exitcode != 0:
                raise RuntimeError(
                    f"participant {process.name} exited with {process.exitcode}"
                )

    intervals: list[tuple[int, int, int, str]] = []
    for instance, requests, starts, ends in results:
        intervals.extend(
            (start, end, request, instance)
            for request, start, end in zip(requests, starts, ends)
        )
    intervals.sort()
    if any(left[1] > right[0] for left, right in zip(intervals, intervals[1:])):
        raise RuntimeError("forward intervals overlapped")
    handoffs = [
        right[0] - left[1]
        for left, right in zip(intervals, intervals[1:])
        if left[3] != right[3] and right[2] <= left[1]
    ]
    if not handoffs:
        raise RuntimeError("no queued cross-instance handoffs were observed")
    p50 = percentile(handoffs, 0.50)
    p99 = percentile(handoffs, 0.99)
    print(
        f"workers={args.workers} handoffs={len(handoffs)} p50_us={p50:.2f} "
        f"p99_us={p99:.2f} max_us={max(handoffs) / 1_000:.2f}"
    )
    if args.max_p99_us is not None and p99 > args.max_p99_us:
        print(
            f"FAIL: p99 {p99:.2f} us exceeds {args.max_p99_us:.2f} us"
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
