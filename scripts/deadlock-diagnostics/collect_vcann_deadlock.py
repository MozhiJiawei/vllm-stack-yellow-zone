#!/usr/bin/env python3
"""Freeze and inspect processes that have loaded vCANN-RT libvruntime.so.

This collector has no vLLM integration. Targets are either explicit ``--pid``
values or processes whose ``/proc/PID/maps`` contains libvruntime.so.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
VCANN_LIBRARY = "libvruntime.so"
VCANN_LOG_DIR = Path("/var/log/enpu/vcann-rt")
GDB_HELPER = Path(__file__).with_name("vcann_trace_gdb.py")
SAFE_LABEL = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")
SAFE_ENV_PREFIXES = ("ENPU_", "ASCEND_", "HCCL_", "XLITE_")
SAFE_ENV_NAMES = {
    "RANK",
    "LOCAL_RANK",
    "WORLD_SIZE",
    "MASTER_ADDR",
    "MASTER_PORT",
    "DEVICE_ID",
}
NPU_DEVICE = re.compile(r"/dev/davinci\d+$")


class CollectionError(RuntimeError):
    pass


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def proc_starttime(pid: int) -> int:
    raw = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    closing = raw.rfind(")")
    if closing < 0:
        raise CollectionError(f"malformed /proc/{pid}/stat")
    return int(raw[closing + 2 :].split()[19])


def proc_state(pid: int) -> str:
    raw = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    closing = raw.rfind(")")
    return raw[closing + 2 :].split()[0] if closing >= 0 else "?"


def read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError:
        return b""


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return f"ERROR: {type(error).__name__}: {error}\n"


def parse_environ(raw: bytes) -> dict[str, str]:
    result: dict[str, str] = {}
    for item in raw.split(b"\0"):
        if b"=" not in item:
            continue
        key_raw, value_raw = item.split(b"=", 1)
        key = key_raw.decode("utf-8", errors="replace")
        if key in SAFE_ENV_NAMES or key.startswith(SAFE_ENV_PREFIXES):
            result[key] = value_raw.decode("utf-8", errors="replace")
    return result


def loaded_vcann(pid: int) -> bool:
    maps = read_text(Path(f"/proc/{pid}/maps"))
    return any(VCANN_LIBRARY in line for line in maps.splitlines())


def npu_device_fds(pid: int) -> list[str]:
    devices: set[str] = set()
    try:
        descriptors = Path(f"/proc/{pid}/fd").iterdir()
        for descriptor in descriptors:
            try:
                target = os.readlink(descriptor)
            except OSError:
                continue
            if NPU_DEVICE.fullmatch(target):
                devices.add(target)
    except OSError:
        return []
    return sorted(devices)


def process_metadata(pid: int) -> dict[str, Any]:
    environment = parse_environ(read_bytes(Path(f"/proc/{pid}/environ")))
    command = read_bytes(Path(f"/proc/{pid}/cmdline")).replace(b"\0", b" ").decode(
        "utf-8", errors="replace"
    ).strip()
    rank_hint = environment.get("LOCAL_RANK") or environment.get("RANK")
    return {
        "pid": pid,
        "pid_starttime": proc_starttime(pid),
        "state": proc_state(pid),
        "rank_hint": rank_hint,
        "command": command,
        "environment": environment,
        "npu_devices": npu_device_fds(pid),
    }


def discover_processes(include_no_device: bool = False) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    for path in Path("/proc").iterdir():
        if not path.name.isdigit():
            continue
        pid = int(path.name)
        if pid == os.getpid():
            continue
        try:
            if loaded_vcann(pid):
                metadata = process_metadata(pid)
                if include_no_device or metadata["npu_devices"]:
                    candidates.append(metadata)
        except (OSError, ValueError, CollectionError):
            continue
    return sorted(candidates, key=lambda item: int(item["pid"]))


def validate_target(target: dict[str, Any]) -> None:
    pid = int(target["pid"])
    try:
        actual = proc_starttime(pid)
    except (OSError, ValueError) as error:
        raise CollectionError(f"pid {pid} is unavailable: {error}") from error
    if actual != int(target["pid_starttime"]):
        raise CollectionError(f"refusing reused pid {pid}: process start time changed")


def select_targets(
    pids: list[int], expected: int | None, include_no_device: bool = False
) -> list[dict[str, Any]]:
    if expected is not None and expected <= 0:
        raise CollectionError("expected process count must be positive")
    if pids:
        if len(set(pids)) != len(pids):
            raise CollectionError("duplicate --pid value")
        targets = []
        for pid in pids:
            if not loaded_vcann(pid):
                raise CollectionError(f"pid {pid} has not loaded {VCANN_LIBRARY}")
            targets.append(process_metadata(pid))
    else:
        targets = discover_processes(include_no_device)
    if expected is not None and len(targets) != expected:
        compact = [
            {key: item.get(key) for key in ("pid", "rank_hint", "state", "command")}
            for item in targets
        ]
        raise CollectionError(
            f"expected {expected} vCANN processes, found {len(targets)}:\n"
            + json.dumps(compact, indent=2)
        )
    if not targets:
        raise CollectionError(f"no process has loaded {VCANN_LIBRARY}")
    return targets


def send_signal(targets: Iterable[dict[str, Any]], sig: signal.Signals) -> None:
    for target in targets:
        validate_target(target)
        os.kill(int(target["pid"]), sig)


def send_signal_best_effort(
    targets: Iterable[dict[str, Any]], sig: signal.Signals
) -> list[str]:
    errors: list[str] = []
    for target in targets:
        try:
            validate_target(target)
            os.kill(int(target["pid"]), sig)
        except (CollectionError, OSError) as error:
            errors.append(f"pid {target.get('pid')}: {error}")
    return errors


def wait_stopped(targets: list[dict[str, Any]], timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    remaining = {int(item["pid"]) for item in targets}
    while remaining and time.monotonic() < deadline:
        for pid in list(remaining):
            if proc_state(pid) in {"T", "t"}:
                remaining.remove(pid)
        if remaining:
            time.sleep(0.02)
    if remaining:
        raise CollectionError(f"processes did not stop: {sorted(remaining)}")


def run_command(command: list[str], timeout: float = 90.0) -> dict[str, Any]:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
        return {
            "command": command,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "error": f"{type(error).__name__}: {error}"}


def write_command(path: Path, result: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8") as stream:
        stream.write("COMMAND: " + " ".join(result["command"]) + "\n")
        if "error" in result:
            stream.write("ERROR: " + result["error"] + "\n")
            return
        stream.write(f"RETURNCODE: {result['returncode']}\n===== STDOUT =====\n")
        stream.write(result["stdout"])
        stream.write("\n===== STDERR =====\n")
        stream.write(result["stderr"])


def target_directory(output: Path, target: dict[str, Any]) -> Path:
    rank = target.get("rank_hint")
    suffix = f"-rank-{rank}" if rank is not None else ""
    return output / f"pid-{target['pid']}{suffix}"


def copy_vcann_logs(pid: int, output: Path) -> list[str]:
    copied: list[str] = []
    if not VCANN_LOG_DIR.is_dir():
        return copied
    pattern = re.compile(rf"_{pid}_[^/]*\.log(?:\.tar\.gz)?$")
    for source in VCANN_LOG_DIR.iterdir():
        if source.is_file() and pattern.search(source.name):
            destination = output / "vcann-logs" / source.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            copied.append(str(destination))
    return copied


def collect_proc(pid: int, output: Path) -> None:
    for name in ("maps", "status", "limits", "sched", "wchan", "syscall"):
        (output / f"proc-{name}.txt").write_text(
            read_text(Path(f"/proc/{pid}/{name}")), encoding="utf-8"
        )
    task_root = Path(f"/proc/{pid}/task")
    threads = []
    for task in sorted(task_root.iterdir(), key=lambda path: int(path.name)):
        tid = int(task.name)
        stat_fields = read_text(task / "stat").split()
        threads.append(
            {
                "tid": tid,
                "comm": read_text(task / "comm").strip(),
                "state": stat_fields[2] if len(stat_fields) > 2 else "?",
                "wchan": read_text(task / "wchan").strip(),
                "syscall": read_text(task / "syscall").strip(),
            }
        )
    atomic_json(output / "threads.json", threads)


def collect_gdb(target: dict[str, Any], output: Path, trace_limit: int) -> dict[str, Any]:
    pid = int(target["pid"])
    if not shutil.which("gdb"):
        return {"error": "gdb is not installed"}
    trace_path = output / "vcann-trace.json"
    command = ["gdb", "-nx", "-q", "-batch"]
    if GDB_HELPER.exists():
        command.extend(["-x", str(GDB_HELPER)])
    command.extend(
        [
            "-p",
            str(pid),
            "-ex",
            "set pagination off",
            "-ex",
            "set print thread-events off",
            "-ex",
            "info threads",
            "-ex",
            "thread apply all bt full",
            "-ex",
            "info sharedlibrary",
        ]
    )
    if GDB_HELPER.exists():
        command.extend(["-ex", f"vcann-trace-dump {trace_path} {trace_limit}"])
    command.extend(["-ex", "detach"])
    result = run_command(command)
    write_command(output / "gdb-native.txt", result)
    validate_target(target)
    os.kill(pid, signal.SIGSTOP)
    wait_stopped([target])
    summary = {
        key: result.get(key) for key in ("returncode", "error") if key in result
    } | {"trace_created": trace_path.exists()}
    if trace_path.exists():
        try:
            trace = json.loads(trace_path.read_text(encoding="utf-8"))
            summary["trace_available"] = bool(trace.get("available"))
            summary["trace_enabled"] = bool(trace.get("enabled"))
            summary["trace_error"] = trace.get("error")
        except (OSError, json.JSONDecodeError) as error:
            summary["trace_error"] = str(error)
    return summary


def capture_target(target: dict[str, Any], output: Path, trace_limit: int) -> dict[str, Any]:
    validate_target(target)
    directory = target_directory(output, target)
    directory.mkdir(parents=True, exist_ok=True)
    atomic_json(directory / "process.json", target)
    collect_proc(int(target["pid"]), directory)
    logs = copy_vcann_logs(int(target["pid"]), directory)
    gdb_result = collect_gdb(target, directory, trace_limit)
    return {"pid": target["pid"], "directory": str(directory), "logs": logs, "gdb": gdb_result}


def trace_summary(path: Path) -> dict[str, Any]:
    try:
        trace = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"trace_state": "unavailable", "trace_error": str(error)}
    if not trace.get("available"):
        return {"trace_state": "unavailable", "trace_error": trace.get("error", "")}
    if not trace.get("enabled"):
        return {"trace_state": "disabled"}
    probe = trace.get("sync_probe", {})
    sync_stream = str(probe.get("stream", ""))
    records = trace.get("records", [])
    matching = [
        record
        for record in records
        if record.get("on_sync_stream")
        and record.get("kind_name") not in {"SCHED_SYNC_BEGIN", "SCHED_SYNC_END"}
    ]
    begin_sequence = int(probe.get("begin_sequence") or 0)
    before = [record for record in matching if int(record.get("sequence") or 0) < begin_sequence]
    after = [record for record in matching if int(record.get("sequence") or 0) > begin_sequence]
    last_before = before[-1] if before else {}
    first_after = after[0] if after else {}
    last = last_before or (matching[-1] if matching else (records[-1] if records else {}))

    def object_label(record: dict[str, Any]) -> str:
        return str(
            record.get("object_name") or record.get("object_symbol") or record.get("object", "")
        )

    return {
        "trace_state": "enabled",
        "sync_active": bool(probe.get("active")),
        "sync_stream": sync_stream,
        "sync_owner": probe.get("owner"),
        "sync_turn": probe.get("schedule_turn"),
        "last_sequence": last.get("sequence"),
        "last_kind": last.get("kind_name", ""),
        "last_object": object_label(last),
        "last_attempt_before_sync_sequence": last_before.get("sequence"),
        "last_attempt_before_sync_kind": last_before.get("kind_name", ""),
        "last_attempt_before_sync_object": object_label(last_before),
        "first_attempt_after_sync_sequence": first_after.get("sequence"),
        "first_attempt_after_sync_kind": first_after.get("kind_name", ""),
        "first_attempt_after_sync_object": object_label(first_after),
    }


def summarize_dirs(capture_dirs: Iterable[Path], output: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for capture_dir in capture_dirs:
        targets_path = capture_dir / "targets.json"
        if not targets_path.exists():
            continue
        manifest = json.loads(targets_path.read_text(encoding="utf-8"))
        for target in manifest["targets"]:
            directory = target_directory(capture_dir, target)
            row = {
                "model": manifest.get("model", ""),
                "pid": target["pid"],
                "rank_hint": target.get("rank_hint"),
                "command": target.get("command", ""),
            }
            row.update(trace_summary(directory / "vcann-trace.json"))
            rows.append(row)
    output.mkdir(parents=True, exist_ok=True)
    atomic_json(output / "vcann-summary.json", rows)
    fieldnames = sorted({key for row in rows for key in row})
    if fieldnames:
        with (output / "vcann-summary.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
    return rows


def command_discover(args: argparse.Namespace) -> int:
    targets = select_targets(args.pid, args.expected_processes, args.include_no_device)
    print(json.dumps(targets, indent=2, sort_keys=True))
    return 0


def command_preflight(args: argparse.Namespace) -> int:
    targets = select_targets(args.pid, args.expected_processes, args.include_no_device)
    checks = {
        "platform": sys.platform,
        "uid": os.getuid(),
        "gdb": shutil.which("gdb"),
        "gdb_helper": str(GDB_HELPER) if GDB_HELPER.exists() else None,
        "ptrace_scope": read_text(Path("/proc/sys/kernel/yama/ptrace_scope")).strip(),
        "self_status": read_text(Path("/proc/self/status")),
        "targets": targets,
    }
    print(json.dumps(checks, indent=2, sort_keys=True))
    return 0 if checks["gdb"] and checks["gdb_helper"] else 2


def command_capture(args: argparse.Namespace) -> int:
    output = Path(args.output).resolve()
    if output.exists() and any(output.iterdir()):
        raise CollectionError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    if not SAFE_LABEL.fullmatch(args.model):
        raise CollectionError("model label may contain only letters, digits, dot, underscore, and dash")
    if args.trace_limit <= 0:
        raise CollectionError("trace limit must be positive")
    if not shutil.which("gdb"):
        raise CollectionError("gdb is not installed")
    if not GDB_HELPER.exists():
        raise CollectionError(f"GDB helper is missing: {GDB_HELPER}")
    targets = select_targets(args.pid, args.expected_processes, args.include_no_device)
    atomic_json(
        output / "targets.json",
        {"schema_version": SCHEMA_VERSION, "model": args.model, "targets": targets},
    )
    stopped: list[dict[str, Any]] = []
    results = []
    try:
        for target in targets:
            validate_target(target)
            os.kill(int(target["pid"]), signal.SIGSTOP)
            stopped.append(target)
        wait_stopped(stopped)
        for target in targets:
            results.append(capture_target(target, output, args.trace_limit))
        atomic_json(output / "capture-results.json", results)
        summarize_dirs([output], output)
    finally:
        if args.resume_after:
            errors = send_signal_best_effort(stopped, signal.SIGCONT)
            for error in errors:
                print(f"WARNING: failed to resume {error}", file=sys.stderr)
    print(f"capture complete: {output}")
    if not args.resume_after:
        print("targets remain frozen; use the resume command after preserving the scene")
    failed = [
        result
        for result in results
        if result["gdb"].get("returncode") != 0
        or not result["gdb"].get("trace_available")
        or not result["gdb"].get("trace_enabled")
    ]
    if failed:
        print(f"ERROR: vCANN GDB capture failed for {len(failed)} process(es)", file=sys.stderr)
        return 2
    return 0


def command_resume(args: argparse.Namespace) -> int:
    capture_dir = Path(args.capture_dir).resolve()
    manifest = json.loads((capture_dir / "targets.json").read_text(encoding="utf-8"))
    targets = manifest["targets"]
    errors = send_signal_best_effort(targets, signal.SIGCONT)
    states = []
    for target in targets:
        try:
            state = proc_state(int(target["pid"]))
        except OSError:
            state = "unavailable"
        states.append({"pid": target["pid"], "state": state})
    print(json.dumps(states, indent=2))
    for error in errors:
        print(f"WARNING: failed to resume {error}", file=sys.stderr)
    return 2 if errors else 0


def command_summarize(args: argparse.Namespace) -> int:
    rows = summarize_dirs(
        [Path(value).resolve() for value in args.capture_dir], Path(args.output).resolve()
    )
    print(json.dumps(rows, indent=2, sort_keys=True))
    return 0


def add_targets(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--pid", type=int, action="append", default=[])
    parser.add_argument("--expected-processes", type=int, default=8)
    parser.add_argument(
        "--include-no-device",
        action="store_true",
        help="include libvruntime processes without an open /dev/davinciN fd",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    discover = commands.add_parser("discover", help="list processes that loaded libvruntime.so")
    add_targets(discover)
    discover.set_defaults(func=command_discover)
    preflight = commands.add_parser(
        "preflight", help="check vCANN targets, GDB, and ptrace context"
    )
    add_targets(preflight)
    preflight.set_defaults(func=command_preflight)
    capture = commands.add_parser("capture", help="freeze targets and capture vCANN state")
    add_targets(capture)
    capture.add_argument("--model", required=True)
    capture.add_argument("--output", required=True)
    capture.add_argument("--trace-limit", type=int, default=4096)
    capture.add_argument("--resume-after", action="store_true")
    capture.set_defaults(func=command_capture)
    resume = commands.add_parser("resume", help="resume processes from a capture manifest")
    resume.add_argument("--capture-dir", required=True)
    resume.set_defaults(func=command_resume)
    summarize = commands.add_parser("summarize", help="merge vCANN traces from captures")
    summarize.add_argument("--capture-dir", action="append", required=True)
    summarize.add_argument("--output", required=True)
    summarize.set_defaults(func=command_summarize)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if sys.platform != "linux":
        print("ERROR: collector must run inside the Linux workload container", file=sys.stderr)
        return 2
    try:
        return int(args.func(args))
    except (CollectionError, OSError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
