#!/usr/bin/env python3
"""Run configured X_ONLY and fixed incomplete-TP8-AllReduce + X cases."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any

from xccl_allreduce_x import ConfigError, Scenario, Settings, execute_case, load_config


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--config", type=Path, required=True)
    result.add_argument("--phase", choices=("preflight", "contention", "all"), default="all")
    result.add_argument("--scenario", action="append", dest="scenario_ids")
    result.add_argument("--kind", choices=("vector", "cube", "fused"))
    result.add_argument("--repeat", type=int)
    result.add_argument("--output", type=Path)
    result.add_argument("--list", action="store_true")
    result.add_argument("--dry-run", action="store_true")
    result.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    result.add_argument("--worker-scenario", help=argparse.SUPPRESS)
    result.add_argument("--worker-shape-index", type=int, help=argparse.SUPPRESS)
    result.add_argument("--worker-phase", choices=("preflight", "contention"), help=argparse.SUPPRESS)
    result.add_argument("--worker-attempt", type=int, help=argparse.SUPPRESS)
    result.add_argument("--worker-result", type=Path, help=argparse.SUPPRESS)
    return result


def select_scenarios(scenarios: list[Scenario], args: argparse.Namespace) -> list[Scenario]:
    selected = scenarios
    if args.scenario_ids:
        requested = set(args.scenario_ids)
        known = {scenario.id for scenario in scenarios}
        unknown = sorted(requested - known)
        if unknown:
            raise ConfigError(f"unknown scenario(s): {', '.join(unknown)}")
        selected = [scenario for scenario in selected if scenario.id in requested]
    if args.kind:
        selected = [scenario for scenario in selected if scenario.kind == args.kind]
    return selected


def scenario_json(scenario: Scenario) -> str:
    return json.dumps(
        {
            "id": scenario.id,
            "operator": scenario.operator,
            "kind": scenario.kind,
            "expected_core": scenario.expected_core,
            "dtype": scenario.dtype,
            "shape": dict(scenario.shape),
            "params": dict(scenario.params),
            "source": scenario.source,
        },
        sort_keys=True,
    )


def stop_process_group(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if sys.platform == "win32":
        process.terminate()
    else:
        os_killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        if sys.platform == "win32":
            process.kill()
        else:
            os_killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=10)


def os_killpg(pid: int, sig: signal.Signals) -> None:
    import os

    os.killpg(pid, sig)


def run_isolated(
    args: argparse.Namespace,
    scenario: Scenario,
    shape_index: int,
    phase: str,
    attempt: int,
    settings: Settings,
) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="xccl-allreduce-x-") as directory:
        result_path = Path(directory) / "result.json"
        command = [
            sys.executable,
            str(Path(__file__).resolve()),
            "--config",
            str(args.config.resolve()),
            "--worker",
            "--worker-scenario",
            scenario.id,
            "--worker-shape-index",
            str(shape_index),
            "--worker-phase",
            phase,
            "--worker-attempt",
            str(attempt),
            "--worker-result",
            str(result_path),
        ]
        print(
            f"MATRIX_CASE_START scenario={scenario.id} phase={phase} attempt={attempt} "
            f"operator={scenario.operator} kind={scenario.kind}",
            flush=True,
        )
        started = time.monotonic()
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=sys.platform != "win32",
        )
        timed_out = False
        try:
            output, _ = process.communicate(timeout=settings.case_timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            stop_process_group(process)
            output, _ = process.communicate()
        print(output, end="" if not output or output.endswith("\n") else "\n", flush=True)
        if timed_out:
            record = {
                "scenario": scenario.id,
                "operator": scenario.operator,
                "kind": scenario.kind,
                "expected_core": scenario.expected_core,
                "dtype": scenario.dtype,
                "shape": dict(scenario.shape),
                "params": dict(scenario.params),
                "source": scenario.source,
                "phase": phase,
                "attempt": attempt,
                "result": "CASE_TIMEOUT",
                "elapsed_seconds": round(time.monotonic() - started, 6),
                "returncode": process.returncode,
                "error": "isolated case exceeded case_timeout",
            }
        elif result_path.is_file():
            record = json.loads(result_path.read_text(encoding="utf-8"))
            record["returncode"] = process.returncode
        else:
            record = {
                "scenario": scenario.id,
                "operator": scenario.operator,
                "kind": scenario.kind,
                "expected_core": scenario.expected_core,
                "dtype": scenario.dtype,
                "shape": dict(scenario.shape),
                "params": dict(scenario.params),
                "source": scenario.source,
                "phase": phase,
                "attempt": attempt,
                "result": "RUNTIME_FAILED",
                "elapsed_seconds": round(time.monotonic() - started, 6),
                "returncode": process.returncode,
                "error": "worker exited without a result record",
            }
        print(
            f"MATRIX_CASE_RESULT scenario={scenario.id} phase={phase} attempt={attempt} "
            f"result={record['result']} returncode={process.returncode}",
            flush=True,
        )
        return record


def append_record(path: Path | None, record: dict[str, Any]) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as output:
        output.write(json.dumps(record, sort_keys=True) + "\n")


def worker_main(args: argparse.Namespace, scenarios: list[Scenario], settings: Settings) -> int:
    if args.worker_scenario is None or args.worker_shape_index is None or args.worker_phase is None or args.worker_attempt is None or args.worker_result is None:
        raise ConfigError("worker arguments are incomplete")
    matching = [scenario for scenario in scenarios if scenario.id == args.worker_scenario]
    if not 0 <= args.worker_shape_index < len(matching):
        raise ConfigError("worker shape index is out of range")
    record = execute_case(matching[args.worker_shape_index], args.worker_phase, settings, args.worker_attempt)
    args.worker_result.parent.mkdir(parents=True, exist_ok=True)
    args.worker_result.write_text(json.dumps(record, sort_keys=True), encoding="utf-8")
    expected = {"PASS", "UNSUPPORTED"} if args.worker_phase == "preflight" else {"PASS", "BLOCKED_BY_A_ALLREDUCE"}
    return 0 if record["result"] in expected else 2


def main() -> int:
    args = parser().parse_args()
    try:
        scenarios, settings, _baseline = load_config(args.config)
        if args.repeat is not None:
            if args.repeat <= 0:
                raise ConfigError("--repeat must be positive")
            settings = Settings(args.repeat, settings.startup_timeout, settings.operator_timeout, settings.hold_confirm_seconds, settings.case_timeout, settings.memory_limit_mib)
        if args.worker:
            return worker_main(args, scenarios, settings)
        selected = select_scenarios(scenarios, args)
    except ConfigError as error:
        parser().error(str(error))

    if args.list or args.dry_run:
        for scenario in selected:
            print(scenario_json(scenario))
        print(f"TOTAL={len(selected)}")
        return 0

    records: list[dict[str, Any]] = []
    grouped: dict[str, list[Scenario]] = {}
    for scenario in selected:
        grouped.setdefault(scenario.id, []).append(scenario)
    for scenario_id, shapes in grouped.items():
        for shape_index, scenario in enumerate(shapes):
            for attempt in range(1, settings.repeat + 1):
                if args.phase in {"preflight", "all"}:
                    preflight = run_isolated(args, scenario, shape_index, "preflight", attempt, settings)
                    records.append(preflight)
                    append_record(args.output, preflight)
                    if args.phase == "all" and preflight["result"] != "PASS":
                        print(f"MATRIX_CASE_SKIP scenario={scenario_id} phase=contention reason=preflight_{preflight['result']}", flush=True)
                        continue
                if args.phase in {"contention", "all"}:
                    contention = run_isolated(args, scenario, shape_index, "contention", attempt, settings)
                    records.append(contention)
                    append_record(args.output, contention)

    counts = Counter(str(record["result"]) for record in records)
    print("MATRIX_SUMMARY " + " ".join(f"{key}={value}" for key, value in sorted(counts.items())))
    failures = {"SETUP_FAILED", "RUNTIME_FAILED", "CASE_TIMEOUT"}
    return 2 if failures & set(counts) else 0


if __name__ == "__main__":
    raise SystemExit(main())
