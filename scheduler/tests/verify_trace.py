from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def percentile(values: list[int], quantile: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(quantile * len(ordered)) - 1)
    return ordered[index] / 1_000


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--expect", choices=("success", "failure"), default="success")
    args = parser.parse_args()
    records = [json.loads(line) for line in args.trace.read_text().splitlines()]
    errors = [record for record in records if record["event"] == "error"]
    if args.expect == "failure":
        assert errors, "expected at least one fail-closed result"
        print(f"verified failure path with {len(errors)} error record(s)")
        return 0

    assert not errors, errors
    intervals = []
    starts = {}
    requests = {}
    for record in records:
        key = (record["instance"], record["iteration"])
        if record["event"] == "request":
            requests[key] = record["timestamp_ns"]
        elif record["event"] == "forward_start":
            starts[key] = record["timestamp_ns"]
        elif record["event"] == "forward_end":
            intervals.append((starts.pop(key), record["timestamp_ns"], key))
    assert not starts
    assert intervals
    intervals.sort()
    for left, right in zip(intervals, intervals[1:]):
        assert left[1] <= right[0], f"overlapping forwards: {left} and {right}"

    sampling = {}
    for record in records:
        key = (record["instance"], record["iteration"])
        if record["event"] == "sampling_start":
            sampling[key] = [record["timestamp_ns"], None]
        elif record["event"] == "sampling_end":
            sampling[key][1] = record["timestamp_ns"]
    overlap = any(
        sample[0] < forward[1] and forward[0] < sample[1]
        for sample_key, sample in sampling.items()
        for forward in intervals
        if sample_key[0] != forward[2][0]
    )
    assert overlap, "expected sampling to overlap the peer's forward"
    waits = [start - requests[key] for start, _, key in intervals]
    handoffs = [
        right[0] - left[1]
        for left, right in zip(intervals, intervals[1:])
        if requests[right[2]] <= left[1]
    ]
    assert handoffs, "expected at least one handoff with a queued peer"
    print(
        f"verified {len(intervals)} non-overlapping forwards and sampling overlap; "
        f"request_to_forward_us p50={percentile(waits, 0.50):.1f} "
        f"p99={percentile(waits, 0.99):.1f}; "
        f"handoff_us p50={percentile(handoffs, 0.50):.1f} "
        f"p99={percentile(handoffs, 0.99):.1f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
