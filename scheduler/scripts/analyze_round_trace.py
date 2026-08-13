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

    events: dict[tuple[str, int], dict[str, int]] = defaultdict(dict)
    files: list[Path] = []
    for path in args.paths:
        files.extend(path.rglob("rounds-*.jsonl") if path.is_dir() else [path])
    for path in files:
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                if record["rank"] != args.rank or record["seq"] == 0:
                    continue
                key = (record["instance"], record["seq"])
                events[key][record["event"]] = record["ts_ns"]

    for instance in ("A", "B"):
        rounds = [value for (name, _), value in events.items() if name == instance]
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


if __name__ == "__main__":
    main()
