from __future__ import annotations

import argparse
import math
import multiprocessing
import os
import tempfile
import time
from pathlib import Path
from typing import Any

from vllm_pair_scheduler import PairSchedulerConfig, SharedMemoryForwardGate


def percentile(values: list[int], quantile: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(quantile * len(ordered)) - 1)
    return ordered[index] / 1_000


def run_worker(
    role: str,
    instance: str,
    pair_id: str,
    shm_dir: str,
    worker_rank: int,
    worker_count: int,
    iterations: int,
    warmup: int,
    execute_us: int,
    sample_us: int,
    barrier: Any,
    output: Any,
) -> None:
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
    records: list[tuple[int, int, int, int, int]] = []
    with SharedMemoryForwardGate(
        config, worker_rank=worker_rank, worker_count=worker_count
    ) as gate:
        barrier.wait()
        for index in range(warmup + iterations):
            request_ns = time.monotonic_ns()
            sequence, grant = gate.enter_forward()
            granted_ns = time.monotonic_ns()
            if execute_us:
                time.sleep(execute_us / 1_000_000)
            if sample_us:
                time.sleep(sample_us / 1_000_000)
            complete_ns = time.monotonic_ns()
            gate.leave_forward(sequence, grant)
            returned_ns = time.monotonic_ns()
            if index >= warmup:
                records.append(
                    (sequence, request_ns, granted_ns, complete_ns, returned_ns)
                )
        output.put((instance, worker_rank, records))


def summarize(label: str, values: list[int]) -> None:
    print(
        f"{label} n={len(values)} p50_us={percentile(values, 0.50):.2f} "
        f"p90_us={percentile(values, 0.90):.2f} "
        f"p99_us={percentile(values, 0.99):.2f} "
        f"max_us={max(values) / 1_000:.2f}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure TP worker wake, completion, and drain dispersion"
    )
    parser.add_argument("--iterations", type=int, default=1_000)
    parser.add_argument("--warmup", type=int, default=100)
    parser.add_argument("--workers", type=int, default=4, choices=(1, 2, 4))
    parser.add_argument("--execute-us", type=int, default=0)
    parser.add_argument("--sample-us", type=int, default=0)
    args = parser.parse_args()
    if min(args.iterations, args.workers) <= 0 or min(
        args.warmup, args.execute_us, args.sample_us
    ) < 0:
        parser.error("counts and durations must be non-negative; iterations positive")

    context = multiprocessing.get_context("spawn")
    barrier = context.Barrier(2 * args.workers)
    output = context.Queue()
    pair_id = f"timeline-{os.getpid()}-{time.monotonic_ns()}"
    with tempfile.TemporaryDirectory(prefix="vllm-pair-timeline-") as shm_dir:
        processes = [
            context.Process(
                target=run_worker,
                args=(
                    role,
                    instance,
                    pair_id,
                    shm_dir,
                    rank,
                    args.workers,
                    args.iterations,
                    args.warmup,
                    args.execute_us,
                    args.sample_us,
                    barrier,
                    output,
                ),
            )
            for role, instance in (("primary", "A"), ("standby", "B"))
            for rank in range(args.workers)
        ]
        for process in processes:
            process.start()
        results = [output.get(timeout=120) for _ in processes]
        for process in processes:
            process.join(timeout=10)
            if process.exitcode != 0:
                raise RuntimeError(
                    f"participant {process.name} exited with {process.exitcode}"
                )

    rounds: dict[tuple[str, int], list[tuple[int, int, int, int]]] = {}
    rank0_grants: dict[str, list[int]] = {"A": [], "B": []}
    for instance, rank, records in results:
        for sequence, request, granted, complete, returned in records:
            rounds.setdefault((instance, sequence), []).append(
                (request, granted, complete, returned)
            )
            if rank == 0:
                rank0_grants[instance].append(granted)

    complete_rounds = [
        records for records in rounds.values() if len(records) == args.workers
    ]
    grant_spread = [
        max(record[1] for record in records)
        - min(record[1] for record in records)
        for records in complete_rounds
    ]
    complete_spread = [
        max(record[2] for record in records)
        - min(record[2] for record in records)
        for records in complete_rounds
    ]
    drain_spread = [
        max(record[3] for record in records)
        - min(record[3] for record in records)
        for records in complete_rounds
    ]
    owner_window = [
        max(record[2] for record in records)
        - min(record[1] for record in records)
        for records in complete_rounds
    ]
    same_instance_interval = [
        right - left
        for grants in rank0_grants.values()
        for left, right in zip(sorted(grants), sorted(grants)[1:])
    ]

    print(
        f"workers={args.workers} execute_us={args.execute_us} "
        f"sample_us={args.sample_us} rounds={len(complete_rounds)}"
    )
    summarize("grant_wake_spread", grant_spread)
    summarize("complete_arrival_spread", complete_spread)
    summarize("drain_return_spread", drain_spread)
    summarize("owner_window", owner_window)
    summarize("same_instance_interval", same_instance_interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
