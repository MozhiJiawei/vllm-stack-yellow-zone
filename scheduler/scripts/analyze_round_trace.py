from __future__ import annotations

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


PHASES = (
    ("gate_wait", "enter_begin", "enter_end"),
    ("execute", "execute_begin", "execute_end"),
    ("forward_to_sample", "execute_end", "sample_begin"),
    ("sample", "sample_begin", "sample_end"),
    ("complete_fence", "leave_begin", "leave_end"),
    ("grant_hold", "enter_end", "leave_begin"),
)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Summarize protocol-v4 execution-round JSONL traces"
    )
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--rank", type=int, default=0)
    args = parser.parse_args()

    events: dict[tuple[str, int, int], dict[str, int]] = defaultdict(dict)
    files: list[Path] = []
    for path in args.paths:
        files.extend(path.rglob("rounds-*.jsonl") if path.is_dir() else [path])
    for path in files:
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                if record["seq"] == 0:
                    continue
                key = (record["instance"], record["seq"], record["rank"])
                events[key][record["event"]] = record["ts_ns"]

    for instance in ("A", "B"):
        rounds = [
            value
            for (name, _, rank), value in events.items()
            if name == instance and rank == args.rank
        ]
        if not rounds:
            continue
        print(f"instance={instance} rank={args.rank} rounds={len(rounds)}")
        for label, start, end in PHASES:
            values = [
                (round_[end] - round_[start]) / 1_000_000
                for round_ in rounds
                if start in round_ and end in round_
            ]
            if not values:
                continue
            print(
                f"  {label:18s} n={len(values):5d} "
                f"mean_ms={statistics.fmean(values):9.3f} "
                f"p50_ms={percentile(values, 0.50):9.3f} "
                f"p90_ms={percentile(values, 0.90):9.3f} "
                f"max_ms={max(values):9.3f}"
            )

    grouped: dict[tuple[str, int], list[dict[str, int]]] = defaultdict(list)
    for (instance, sequence, _), round_ in events.items():
        grouped[(instance, sequence)].append(round_)
    for instance in ("A", "B"):
        rounds = [
            values
            for (name, _), values in grouped.items()
            if name == instance and len(values) > 1
        ]
        if not rounds:
            continue
        print(f"instance={instance} tp_barriers rounds={len(rounds)}")
        barrier_phases = (
            ("enter_return_spread", "enter_end"),
            ("complete_arrival_spread", "leave_begin"),
            ("leave_return_spread", "leave_end"),
        )
        for label, event in barrier_phases:
            values = [
                (max(rank[event] for rank in round_) - min(rank[event] for rank in round_))
                / 1_000_000
                for round_ in rounds
                if all(event in rank for rank in round_)
            ]
            if not values:
                continue
            print(
                f"  {label:23s} n={len(values):5d} "
                f"mean_ms={statistics.fmean(values):9.3f} "
                f"p50_ms={percentile(values, 0.50):9.3f} "
                f"p90_ms={percentile(values, 0.90):9.3f} "
                f"max_ms={max(values):9.3f}"
            )


if __name__ == "__main__":
    main()
