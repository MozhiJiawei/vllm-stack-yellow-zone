from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

from vllm_pair_scheduler import PairSchedulerConfig, SharedMemoryForwardGate


def emit(
    trace: Path,
    instance: str,
    event: str,
    iteration: int = -1,
    *,
    timestamp_ns: int | None = None,
    **extra,
) -> None:
    record = {
        "instance": instance,
        "event": event,
        "iteration": iteration,
        "timestamp_ns": time.monotonic_ns() if timestamp_ns is None else timestamp_ns,
        **extra,
    }
    fd = os.open(trace, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o660)
    try:
        os.write(fd, (json.dumps(record, sort_keys=True) + "\n").encode())
    finally:
        os.close(fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--role", choices=("primary", "standby"), required=True)
    parser.add_argument("--instance", choices=("A", "B"), required=True)
    parser.add_argument("--pair", required=True)
    parser.add_argument("--shm-dir", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--forward-ms", type=float, default=2)
    parser.add_argument("--sampling-ms", type=float, default=3)
    parser.add_argument("--forward-timeout-ms", type=int, default=1000)
    parser.add_argument("--hang-first-ms", type=float, default=0)
    parser.add_argument("--hang-forever-first", action="store_true")
    parser.add_argument("--fail-first", action="store_true")
    parser.add_argument("--crash-after-open", action="store_true")
    parser.add_argument("--start-delay-ms", type=float, default=0)
    parser.add_argument("--linger-ms", type=float, default=0)
    args = parser.parse_args()

    config = PairSchedulerConfig(
        mode="elastic",
        role=args.role,
        instance_id=args.instance,
        pair_id=args.pair,
        shm_dir=args.shm_dir,
        init_timeout_ms=2_000,
        forward_timeout_ms=args.forward_timeout_ms,
        heartbeat_ms=10,
        peer_timeout_ms=100,
    )
    try:
        with SharedMemoryForwardGate(config) as gate:
            emit(args.trace, args.instance, "initialized")
            if args.crash_after_open:
                os._exit(91)
            if args.start_delay_ms:
                time.sleep(args.start_delay_ms / 1000)
            for iteration in range(args.iterations):
                emit(args.trace, args.instance, "request", iteration)
                grant = gate.acquire()
                forward_start = time.monotonic_ns()
                emit(
                    args.trace,
                    args.instance,
                    "forward_start",
                    iteration,
                    timestamp_ns=forward_start,
                    grant=grant,
                )
                duration = (
                    args.hang_first_ms
                    if iteration == 0 and args.hang_first_ms
                    else args.forward_ms
                )
                if iteration == 0 and args.hang_forever_first:
                    while True:
                        time.sleep(60)
                time.sleep(duration / 1000)
                if iteration == 0 and args.fail_first:
                    gate.fail(220)
                    raise RuntimeError("injected execute_model failure")
                forward_end = time.monotonic_ns()
                gate.complete(grant)
                emit(
                    args.trace,
                    args.instance,
                    "forward_end",
                    iteration,
                    timestamp_ns=forward_end,
                    grant=grant,
                )
                emit(args.trace, args.instance, "sampling_start", iteration)
                time.sleep(args.sampling_ms / 1000)
                emit(args.trace, args.instance, "sampling_end", iteration)
            if args.linger_ms:
                time.sleep(args.linger_ms / 1000)
            emit(args.trace, args.instance, "closed")
        return 0
    except BaseException as exc:
        emit(args.trace, args.instance, "error", detail=repr(exc))
        print(f"{args.instance}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
