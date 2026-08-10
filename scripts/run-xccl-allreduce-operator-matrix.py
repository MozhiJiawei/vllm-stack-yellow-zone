#!/usr/bin/env python3
"""Run isolated B-operator cases against one fixed incomplete TP8 AllReduce."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


RESULT_PATTERN = re.compile(r"RESULT=([A-Z0-9_]+)")


@dataclass(frozen=True)
class Case:
    name: str
    suite: str
    operator: str
    batch_size: int = 4
    vocab_size: int = 151_936
    dtype: str = "float32"
    top_k: int = 50
    top_p: float = 0.9


CASES = (
    Case("control.sigmoid.f32.b4.v151936", "control", "sigmoid"),
    Case("control.silu.f32.b4.v151936", "control", "silu"),
    Case("control.softmax.f32.b4.v151936", "control", "softmax"),
    Case("control.sort.f32.b4.v151936", "control", "sort"),
    Case("control.sigmoid.bf16.b4.v151936", "control", "sigmoid", dtype="bfloat16"),
    Case("custom.both.f32.b1.v151936", "custom", "topk-topp", batch_size=1),
    Case("custom.both.f32.b4.v32000", "custom", "topk-topp", vocab_size=32_000),
    Case("custom.both.f32.b4.v151936", "custom", "topk-topp"),
    Case("custom.both.f32.b16.v151936", "custom", "topk-topp", batch_size=16),
    Case("custom.both.f16.b4.v151936", "custom", "topk-topp", dtype="float16"),
    Case("custom.both.bf16.b4.v151936", "custom", "topk-topp", dtype="bfloat16"),
    Case("custom.topk_only.f32.b4.v151936", "custom", "topk-only"),
    Case("custom.topp_only.f32.b4.v151936", "custom", "topp-only"),
    Case("custom.both.f32.b4.v151936.k1", "custom", "topk-topp", top_k=1),
    Case("custom.both.f32.b4.v151936.p05", "custom", "topk-topp", top_p=0.5),
)
CASE_BY_NAME = {case.name: case for case in CASES}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--suite", choices=("all", "control", "custom"), default="all")
    parser.add_argument("--case", action="append", dest="case_names")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--case-timeout", type=float, default=900)
    parser.add_argument("--operator-timeout", type=float, default=30)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.repeat <= 0 or args.case_timeout <= 0 or args.operator_timeout <= 0:
        parser.error("repeat and timeouts must be positive")
    return args


def select_cases(args: argparse.Namespace) -> list[Case]:
    if args.case_names:
        unknown = sorted(set(args.case_names) - CASE_BY_NAME.keys())
        if unknown:
            raise SystemExit(f"unknown case(s): {', '.join(unknown)}")
        return [CASE_BY_NAME[name] for name in args.case_names]
    return [case for case in CASES if args.suite == "all" or case.suite == args.suite]


def stop_process_group(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=10)


def run_case(
    repro: Path, case: Case, attempt: int, args: argparse.Namespace
) -> dict[str, object]:
    command = [
        sys.executable,
        str(repro),
        "--operator",
        case.operator,
        "--batch-size",
        str(case.batch_size),
        "--vocab-size",
        str(case.vocab_size),
        "--dtype",
        case.dtype,
        "--top-k",
        str(case.top_k),
        "--top-p",
        str(case.top_p),
        "--operator-timeout",
        str(args.operator_timeout),
    ]
    print(
        f"MATRIX_CASE_START name={case.name} attempt={attempt} "
        f"operator={case.operator} shape={case.batch_size}x{case.vocab_size} "
        f"dtype={case.dtype} top_k={case.top_k} top_p={case.top_p:g}",
        flush=True,
    )
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    timed_out = False
    try:
        output, _ = process.communicate(timeout=args.case_timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        stop_process_group(process)
        output, _ = process.communicate()
    elapsed = time.monotonic() - started
    print(output, end="" if output.endswith("\n") or not output else "\n", flush=True)
    matches = RESULT_PATTERN.findall(output)
    result = "CASE_TIMEOUT" if timed_out else (matches[-1] if matches else "NO_RESULT")
    record: dict[str, object] = {
        **asdict(case),
        "attempt": attempt,
        "result": result,
        "returncode": process.returncode,
        "elapsed_seconds": round(elapsed, 6),
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }
    print(
        f"MATRIX_CASE_RESULT name={case.name} attempt={attempt} result={result} "
        f"returncode={process.returncode} elapsed={elapsed:.3f}s",
        flush=True,
    )
    return record


def append_record(path: Path | None, record: dict[str, object]) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, sort_keys=True) + "\n")


def main() -> int:
    args = parse_args()
    selected = select_cases(args)
    if args.list:
        for case in selected:
            print(json.dumps(asdict(case), sort_keys=True))
        print(f"TOTAL={len(selected)}")
        return 0

    repro = Path(__file__).with_name("repro-tp8-xlite-xccl-vs-topk-topp.py")
    records = []
    print(
        f"MATRIX_START cases={len(selected)} repeat={args.repeat} "
        f"operator_timeout={args.operator_timeout:g}",
        flush=True,
    )
    for case in selected:
        for attempt in range(1, args.repeat + 1):
            record = run_case(repro, case, attempt, args)
            records.append(record)
            append_record(args.output, record)

    counts: dict[str, int] = {}
    for record in records:
        result = str(record["result"])
        counts[result] = counts.get(result, 0) + 1
    print("MATRIX_SUMMARY " + " ".join(f"{key}={value}" for key, value in sorted(counts.items())))
    classified = {"PASS", "BLOCKED_BY_A_ALLREDUCE"}
    return 2 if set(counts) - classified else 0


if __name__ == "__main__":
    raise SystemExit(main())
