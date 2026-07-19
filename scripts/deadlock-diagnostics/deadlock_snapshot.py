#!/usr/bin/env python3
"""Capture and summarize a frozen vLLM Ascend TP worker deadlock scene.

The collector uses metadata emitted by
``vllm_ascend.diagnostics.deadlock_dump``.  It never discovers targets using
process-name matching and validates Linux process start times before every
signal to avoid PID-reuse accidents.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
DEFAULT_DIAG_DIR = "/tmp/vllm-deadlock-diag"
DEFAULT_DELAYS_MS = "0,100,500,2000"
DEFAULT_TRACE_SYSCALLS = (
    "futex,ioctl,poll,ppoll,epoll_wait,epoll_pwait,recvfrom,recvmsg,"
    "sendto,sendmsg,read,write"
)
INTERESTING_LIBRARY = re.compile(
    r"(xlite|xccl|torch_npu|ascend|hccl|runtime)", re.IGNORECASE
)
SAFE_IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")


class CollectionError(RuntimeError):
    pass


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def read_text(path: Path) -> tuple[str | None, str | None]:
    try:
        return path.read_text(encoding="utf-8", errors="replace"), None
    except OSError as error:
        return None, f"{type(error).__name__}: {error}"


def proc_starttime(pid: int) -> int:
    stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    closing_paren = stat.rfind(")")
    if closing_paren < 0:
        raise CollectionError(f"malformed /proc/{pid}/stat")
    return int(stat[closing_paren + 2 :].split()[19])


def proc_state(pid: int) -> str:
    stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    closing_paren = stat.rfind(")")
    if closing_paren < 0:
        return "?"
    return stat[closing_paren + 2 :].split()[0]


def validate_worker(worker: dict[str, Any]) -> None:
    pid = int(worker["pid"])
    expected = int(worker["pid_starttime"])
    try:
        actual = proc_starttime(pid)
    except (OSError, ValueError) as error:
        raise CollectionError(
            f"model={worker['model']} rank={worker['rank']} pid={pid} is unavailable: {error}"
        )
    if actual != expected:
        raise CollectionError(
            f"refusing pid={pid}: starttime changed from {expected} to {actual} "
            f"for model={worker['model']} rank={worker['rank']}"
        )


def validate_identifier(kind: str, value: str) -> None:
    if not SAFE_IDENTIFIER.fullmatch(value):
        raise CollectionError(
            f"invalid {kind} {value!r}; use letters, digits, dot, underscore, or dash"
        )


def metadata_dir(diag_dir: str, run_id: str, model: str) -> Path:
    validate_identifier("run ID", run_id)
    validate_identifier("model ID", model)
    return Path(diag_dir) / run_id / f"model-{model}"


def load_workers(
    diag_dir: str,
    run_id: str,
    model: str,
    expected_workers: int | None,
    *,
    validate: bool = True,
) -> list[dict[str, Any]]:
    directory = metadata_dir(diag_dir, run_id, model)
    workers: list[dict[str, Any]] = []
    for path in sorted(directory.glob("rank-*.json")):
        try:
            worker = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise CollectionError(f"cannot read metadata {path}: {error}") from error
        worker["metadata_file"] = str(path)
        if worker.get("run_id") != run_id or worker.get("model") != model:
            raise CollectionError(f"metadata identity mismatch in {path}")
        if validate:
            validate_worker(worker)
        workers.append(worker)
    if not workers:
        raise CollectionError(f"no worker metadata found under {directory}")
    ranks = [int(worker["rank"]) for worker in workers]
    if len(ranks) != len(set(ranks)):
        raise CollectionError(f"duplicate ranks in {directory}: {ranks}")
    if expected_workers is not None and len(workers) != expected_workers:
        raise CollectionError(
            f"expected {expected_workers} workers in {directory}, found {len(workers)}"
        )
    return sorted(workers, key=lambda worker: int(worker["rank"]))


def thread_snapshot(pid: int) -> dict[str, Any]:
    result: dict[str, Any] = {
        "pid": pid,
        "captured_monotonic_ns": time.monotonic_ns(),
        "threads": [],
    }
    task_dir = Path(f"/proc/{pid}/task")
    try:
        tids = sorted((int(path.name) for path in task_dir.iterdir()), key=int)
    except OSError as error:
        result["error"] = f"{type(error).__name__}: {error}"
        return result
    for tid in tids:
        base = task_dir / str(tid)
        thread: dict[str, Any] = {"tid": tid}
        for name in ("comm", "wchan", "syscall", "stack", "stat"):
            value, error = read_text(base / name)
            if value is not None:
                thread[name] = value.strip()
            else:
                thread[f"{name}_error"] = error
        result["threads"].append(thread)
    return result


def snapshot_all(workers: list[dict[str, Any]]) -> list[dict[str, Any]]:
    snapshots: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=len(workers)) as pool:
        futures = {
            pool.submit(thread_snapshot, int(worker["pid"])): worker
            for worker in workers
        }
        for future in as_completed(futures):
            worker = futures[future]
            snapshot = future.result()
            snapshot.update(
                model=worker["model"], rank=worker["rank"], npu=worker.get("npu")
            )
            snapshots.append(snapshot)
    return sorted(snapshots, key=lambda item: int(item["rank"]))


def send_signal_all(
    workers: list[dict[str, Any]],
    signum: signal.Signals,
    *,
    marker: str | None = None,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for worker in workers:
        validate_worker(worker)
        before = time.monotonic_ns()
        try:
            if signum == signal.SIGUSR1 and marker:
                with Path(worker["stack_file"]).open(
                    "a", encoding="utf-8"
                ) as stack_file:
                    stack_file.write(
                        f"# collector {marker} send_monotonic_ns={before}\n"
                    )
            os.kill(int(worker["pid"]), signum)
        except OSError as error:
            raise CollectionError(
                f"failed to send {signum.name} to model={worker['model']} "
                f"rank={worker['rank']} pid={worker['pid']}: {error}"
            ) from error
        events.append(
            {
                "model": worker["model"],
                "rank": worker["rank"],
                "pid": worker["pid"],
                "signal": signum.name,
                "sent_monotonic_ns": before,
            }
        )
    return events


def wait_for_stopped(
    workers: list[dict[str, Any]], timeout: float = 5.0
) -> dict[int, str]:
    deadline = time.monotonic() + timeout
    states: dict[int, str] = {}
    while time.monotonic() < deadline:
        try:
            states = {
                int(worker["pid"]): proc_state(int(worker["pid"])) for worker in workers
            }
        except OSError as error:
            raise CollectionError(
                f"worker disappeared while waiting for SIGSTOP: {error}"
            ) from error
        if all(state in {"T", "t"} for state in states.values()):
            return states
        time.sleep(0.01)
    raise CollectionError(f"not all workers stopped within {timeout}s: {states}")


def copy_python_stacks(workers: list[dict[str, Any]], output: Path) -> None:
    for worker in workers:
        source = Path(worker["stack_file"])
        target = output / f"rank-{worker['rank']}" / "python-stacks.log"
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copy2(source, target)
        except OSError as error:
            target.with_suffix(".error.txt").write_text(str(error), encoding="utf-8")


def run_command(command: list[str], timeout: float = 90.0) -> dict[str, Any]:
    started = time.monotonic_ns()
    try:
        completed = subprocess.run(
            command, capture_output=True, text=True, timeout=timeout, check=False
        )
        return {
            "command": command,
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "started_monotonic_ns": started,
            "finished_monotonic_ns": time.monotonic_ns(),
        }
    except (OSError, subprocess.TimeoutExpired) as error:
        return {
            "command": command,
            "error": f"{type(error).__name__}: {error}",
            "started_monotonic_ns": started,
            "finished_monotonic_ns": time.monotonic_ns(),
        }


def ptrace_child_probe() -> dict[str, Any]:
    gdb = shutil.which("gdb")
    if not gdb:
        return {"available": False, "reason": "gdb not installed"}
    child = subprocess.Popen(
        [sys.executable, "-c", "import time; time.sleep(30)"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        result = run_command(
            [gdb, "-nx", "-q", "-batch", "-p", str(child.pid), "-ex", "detach"],
            timeout=15,
        )
        return {
            "available": result.get("returncode") == 0,
            "returncode": result.get("returncode"),
            "error": result.get("error"),
            "stderr": result.get("stderr", "")[-2000:],
            "scope": "collector child only; sibling worker attach may be stricter",
        }
    finally:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)


def write_command_result(path: Path, result: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        output.write(f"COMMAND: {' '.join(result['command'])}\n")
        if "error" in result:
            output.write(f"ERROR: {result['error']}\n")
            return
        output.write(f"RETURNCODE: {result['returncode']}\n===== STDOUT =====\n")
        output.write(result["stdout"])
        output.write("\n===== STDERR =====\n")
        output.write(result["stderr"])


def collect_native_stack(worker: dict[str, Any], output: Path) -> dict[str, Any]:
    pid = int(worker["pid"])
    rank_dir = output / f"rank-{worker['rank']}"
    rank_dir.mkdir(parents=True, exist_ok=True)
    validate_worker(worker)

    results: dict[str, Any] = {"rank": worker["rank"], "pid": pid}
    if shutil.which("eu-stack"):
        eu_result = run_command(["eu-stack", "-p", str(pid)])
        write_command_result(rank_dir / "eu-stack.txt", eu_result)
        results["eu_stack"] = {
            key: eu_result.get(key)
            for key in ("returncode", "error")
            if key in eu_result
        }
    else:
        results["eu_stack"] = {"error": "eu-stack not installed"}

    if shutil.which("gdb"):
        gdb_result = run_command(
            [
                "gdb",
                "-nx",
                "-q",
                "-batch",
                "-p",
                str(pid),
                "-ex",
                "set pagination off",
                "-ex",
                "set print thread-events off",
                "-ex",
                "info threads",
                "-ex",
                "thread apply all bt",
                "-ex",
                "info sharedlibrary",
                "-ex",
                "detach",
            ]
        )
        write_command_result(rank_dir / "gdb-native-bt.txt", gdb_result)
        results["gdb"] = {
            key: gdb_result.get(key)
            for key in ("returncode", "error")
            if key in gdb_result
        }
        # Some debugger versions resume a previously stopped tracee when they
        # detach. Reassert the group stop so later ranks are inspected against
        # the same preserved deadlock scene.
        validate_worker(worker)
        os.kill(pid, signal.SIGSTOP)
        wait_for_stopped([worker])
    else:
        results["gdb"] = {"error": "gdb not installed"}

    for proc_name in ("maps", "status", "limits"):
        value, error = read_text(Path(f"/proc/{pid}/{proc_name}"))
        (rank_dir / f"proc-{proc_name}.txt").write_text(
            value if value is not None else error or "", encoding="utf-8"
        )
    return results


def library_paths(workers: list[dict[str, Any]]) -> list[Path]:
    paths: set[Path] = set()
    for worker in workers:
        maps, _ = read_text(Path(f"/proc/{worker['pid']}/maps"))
        if not maps:
            continue
        for line in maps.splitlines():
            fields = line.split()
            if (
                len(fields) >= 6
                and fields[-1].startswith("/")
                and INTERESTING_LIBRARY.search(fields[-1])
            ):
                paths.add(Path(fields[-1]))
    return sorted(paths, key=str)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def collect_libraries(workers: list[dict[str, Any]]) -> list[dict[str, Any]]:
    libraries: list[dict[str, Any]] = []
    readelf = shutil.which("readelf")
    for path in library_paths(workers):
        item: dict[str, Any] = {"path": str(path)}
        try:
            item["size"] = path.stat().st_size
            item["sha256"] = sha256(path)
        except OSError as error:
            item["error"] = f"{type(error).__name__}: {error}"
        if readelf:
            note = run_command([readelf, "-n", str(path)], timeout=30)
            text = note.get("stdout", "")
            build_ids = [
                line.strip() for line in text.splitlines() if "Build ID:" in line
            ]
            item["build_id"] = (
                build_ids[0].split("Build ID:", 1)[1].strip() if build_ids else None
            )
        libraries.append(item)
    return libraries


def parse_delays(raw: str) -> list[int]:
    try:
        delays = [int(value) for value in raw.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "sample delays must be comma-separated integers"
        ) from error
    if (
        not delays
        or delays[0] != 0
        or any(value < 0 for value in delays)
        or delays != sorted(delays)
    ):
        raise argparse.ArgumentTypeError(
            "sample delays must be sorted, non-negative, and start at 0"
        )
    return delays


def preflight(args: argparse.Namespace) -> int:
    workers = load_workers(
        args.diag_dir, args.run_id, args.model, args.expected_workers
    )
    checks: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "platform": sys.platform,
        "uid": os.getuid(),
        "workers": [
            {
                key: worker.get(key)
                for key in ("model", "rank", "npu", "pid", "pid_starttime")
            }
            for worker in workers
        ],
        "commands": {
            name: shutil.which(name)
            for name in ("gdb", "eu-stack", "readelf", "strace")
        },
    }
    for path in (Path("/proc/sys/kernel/yama/ptrace_scope"), Path("/proc/self/status")):
        value, error = read_text(path)
        checks[str(path)] = value if value is not None else error
    checks["proc_access"] = snapshot_all(workers)
    checks["ptrace_child_probe"] = ptrace_child_probe()
    print(json.dumps(checks, indent=2, sort_keys=True))
    return 0


def capture(args: argparse.Namespace) -> int:
    workers = load_workers(
        args.diag_dir, args.run_id, args.model, args.expected_workers
    )
    output = Path(args.output).resolve()
    if output.exists() and any(output.iterdir()):
        raise CollectionError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    atomic_json(
        output / "targets.json",
        {
            "schema_version": SCHEMA_VERSION,
            "run_id": args.run_id,
            "model": args.model,
            "workers": workers,
            "created_time_ns": time.time_ns(),
        },
    )
    delays = parse_delays(args.sample_delays_ms)
    started = time.monotonic_ns()
    samples: list[dict[str, Any]] = []
    for sample_id, delay_ms in enumerate(delays):
        target = started + delay_ms * 1_000_000
        remaining = (target - time.monotonic_ns()) / 1_000_000_000
        if remaining > 0:
            time.sleep(remaining)
        signal_events = send_signal_all(
            workers, signal.SIGUSR1, marker=f"sample={sample_id}"
        )
        sample = {
            "sample_id": sample_id,
            "target_delay_ms": delay_ms,
            "captured_monotonic_ns": time.monotonic_ns(),
            "signal_events": signal_events,
            "processes": snapshot_all(workers),
        }
        atomic_json(output / "samples" / f"sample-{sample_id:02d}.json", sample)
        samples.append({"sample_id": sample_id, "target_delay_ms": delay_ms})

    # Give faulthandler a brief chance to flush before copying its output.
    time.sleep(0.05)
    copy_python_stacks(workers, output)
    freeze_events: list[dict[str, Any]] = []
    frozen_states: dict[int, str] = {}
    native_results: list[dict[str, Any]] = []
    if args.freeze:
        freeze_events = send_signal_all(workers, signal.SIGSTOP)
        frozen_states = wait_for_stopped(workers)
        atomic_json(
            output / "freeze-state.json",
            {
                "workers": workers,
                "freeze_events": freeze_events,
                "frozen_states": frozen_states,
                "resume_command": (
                    f"python3 {Path(__file__).resolve()} resume --diag-dir {args.diag_dir} "
                    f"--run-id {args.run_id} --model {args.model} "
                    f"--expected-workers {args.expected_workers}"
                ),
            },
        )
        atomic_json(output / "frozen-proc-snapshot.json", snapshot_all(workers))
        for worker in workers:
            native_results.append(collect_native_stack(worker, output))
        atomic_json(output / "libraries.json", collect_libraries(workers))

    manifest = {
        "schema_version": SCHEMA_VERSION,
        "run_id": args.run_id,
        "model": args.model,
        "diag_dir": args.diag_dir,
        "output": str(output),
        "samples": samples,
        "workers": workers,
        "freeze_requested": args.freeze,
        "freeze_events": freeze_events,
        "frozen_states": frozen_states,
        "native_results": native_results,
        "completed_time_ns": time.time_ns(),
        "completed_monotonic_ns": time.monotonic_ns(),
    }
    atomic_json(output / "manifest.json", manifest)
    summarize_dirs([output], output)
    print(f"capture complete: {output}")
    if args.freeze:
        print(
            "workers remain SIGSTOP-frozen; use the resume command after preserving the scene"
        )
    return 0


def status(args: argparse.Namespace) -> int:
    workers = load_workers(
        args.diag_dir,
        args.run_id,
        args.model,
        args.expected_workers,
        validate=False,
    )
    rows = []
    for worker in workers:
        row = {
            "model": worker["model"],
            "rank": worker["rank"],
            "npu": worker.get("npu"),
            "pid": worker["pid"],
        }
        try:
            validate_worker(worker)
            row["state"] = proc_state(int(worker["pid"]))
        except (CollectionError, OSError) as error:
            row["state"] = "unavailable"
            row["error"] = str(error)
        rows.append(row)
    print(json.dumps(rows, indent=2))
    return 0


def resume(args: argparse.Namespace) -> int:
    workers = load_workers(
        args.diag_dir,
        args.run_id,
        args.model,
        args.expected_workers,
        validate=False,
    )
    events: list[dict[str, Any]] = []
    for worker in workers:
        try:
            events.extend(send_signal_all([worker], signal.SIGCONT))
        except CollectionError as error:
            events.append(
                {
                    "model": worker["model"],
                    "rank": worker["rank"],
                    "pid": worker["pid"],
                    "signal": "SIGCONT",
                    "error": str(error),
                }
            )
    print(json.dumps(events, indent=2))
    return 0


def trace_worker(
    worker: dict[str, Any], output: Path, duration: float, syscalls: str
) -> dict[str, Any]:
    validate_worker(worker)
    rank_dir = output / f"rank-{worker['rank']}"
    rank_dir.mkdir(parents=True, exist_ok=True)
    prefix = rank_dir / "strace"
    command = [
        "strace",
        "-ff",
        "-qq",
        "-ttt",
        "-T",
        "-e",
        f"trace={syscalls}",
        "-o",
        str(prefix),
        "-p",
        str(worker["pid"]),
    ]
    started = time.monotonic_ns()
    try:
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
    except OSError as error:
        return {"rank": worker["rank"], "pid": worker["pid"], "error": str(error)}
    time.sleep(duration)
    if process.poll() is None:
        process.terminate()
    try:
        stdout, stderr = process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate(timeout=5)
    result = {
        "rank": worker["rank"],
        "pid": worker["pid"],
        "command": command,
        "returncode": process.returncode,
        "stdout": stdout,
        "stderr": stderr,
        "started_monotonic_ns": started,
        "finished_monotonic_ns": time.monotonic_ns(),
    }
    if process.returncode not in {0, -signal.SIGTERM}:
        result["error"] = f"strace exited with status {process.returncode}"
    return result


def trace(args: argparse.Namespace) -> int:
    if args.duration <= 0:
        raise CollectionError("trace duration must be positive")
    if not shutil.which("strace"):
        raise CollectionError("strace is not installed")
    workers = load_workers(
        args.diag_dir, args.run_id, args.model, args.expected_workers
    )
    selected_ranks = set(args.rank)
    selected = [worker for worker in workers if int(worker["rank"]) in selected_ranks]
    found = {int(worker["rank"]) for worker in selected}
    if found != selected_ranks:
        raise CollectionError(
            f"requested ranks not found: {sorted(selected_ranks - found)}"
        )
    output = Path(args.output).resolve()
    if output.exists() and any(output.iterdir()):
        raise CollectionError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=len(selected)) as pool:
        futures = [
            pool.submit(trace_worker, worker, output, args.duration, args.syscalls)
            for worker in selected
        ]
        results = [future.result() for future in futures]
    atomic_json(
        output / "trace-manifest.json",
        {
            "schema_version": SCHEMA_VERSION,
            "run_id": args.run_id,
            "model": args.model,
            "duration": args.duration,
            "syscalls": args.syscalls,
            "results": results,
        },
    )
    print(json.dumps(results, indent=2))
    return 0 if all("error" not in result for result in results) else 2


def top_kernel_wait(process: dict[str, Any]) -> tuple[str, str, str]:
    pid = int(process["pid"])
    threads = process.get("threads", [])
    main = next(
        (thread for thread in threads if int(thread["tid"]) == pid),
        threads[0] if threads else {},
    )
    return (
        str(main.get("comm", "")),
        str(main.get("wchan", "")),
        str(main.get("syscall", "")),
    )


def python_top_frames(path: Path, limit: int = 4) -> str:
    text, _ = read_text(path)
    if not text:
        return ""
    latest = text.rsplit("# collector sample=", 1)[-1]
    frames: list[str] = []
    thread = ""
    waiting_for_top = False
    for line in latest.splitlines():
        stripped = line.strip()
        if stripped.startswith("Thread ") or stripped.startswith("Current thread"):
            thread = stripped.rstrip(":")
            waiting_for_top = True
        elif waiting_for_top and stripped.startswith('File "'):
            frames.append(f"{thread}: {stripped}")
            waiting_for_top = False
            if len(frames) >= limit:
                break
    return " | ".join(frames)


def native_top_frame(path: Path) -> str:
    text, _ = read_text(path)
    if not text:
        return ""
    return next(
        (line.strip() for line in text.splitlines() if line.lstrip().startswith("#0")),
        "",
    )


def classify(
    wchan: str, syscall_text: str, native_text: str, python_text: str = ""
) -> str:
    combined = f"{wchan}\n{syscall_text}\n{native_text}\n{python_text}".lower()
    if "xccl" in combined or "allreduce" in combined or "all_reduce" in combined:
        return "XCCL_COLLECTIVE_WAIT"
    if "xlite" in combined and "forward" in combined:
        return "XLITE_FORWARD_WAIT"
    if any(
        value in combined
        for value in (
            "streamsync",
            "streamsynchronize",
            "synchronizestream",
            "eventsynchronize",
            "synchronizeevent",
        )
    ):
        return "NPU_STREAM_SYNC_WAIT"
    if "ioctl" in combined or "do_vfs_ioctl" in combined:
        return "NPU_RUNTIME_IOCTL_WAIT"
    if "futex" in combined:
        return "HOST_BARRIER_WAIT"
    if any(value in combined for value in ("epoll", "poll", "recv", "messagequeue")):
        return "RPC_OR_QUEUE_WAIT"
    return "UNKNOWN_NATIVE_ADDRESS"


def summarize_dirs(capture_dirs: Iterable[Path], output: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for capture_dir in capture_dirs:
        manifest_path = capture_dir / "manifest.json"
        if not manifest_path.exists():
            continue
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        sample_paths = sorted((capture_dir / "samples").glob("sample-*.json"))
        samples = [
            json.loads(path.read_text(encoding="utf-8")) for path in sample_paths
        ]
        frozen_path = capture_dir / "frozen-proc-snapshot.json"
        frozen = (
            json.loads(frozen_path.read_text(encoding="utf-8"))
            if frozen_path.exists()
            else []
        )
        latest = frozen or (samples[-1]["processes"] if samples else [])
        by_rank = {int(process["rank"]): process for process in latest}
        for worker in manifest["workers"]:
            rank = int(worker["rank"])
            process = by_rank.get(rank, {"pid": worker["pid"], "threads": []})
            comm, wchan, syscall_text = top_kernel_wait(process)
            native_path = capture_dir / f"rank-{rank}" / "gdb-native-bt.txt"
            native_text, _ = read_text(native_path)
            python_top = python_top_frames(
                capture_dir / f"rank-{rank}" / "python-stacks.log"
            )
            native_top = native_top_frame(native_path)
            waits: list[tuple[str, str]] = []
            for sample in samples:
                sampled = next(
                    (item for item in sample["processes"] if int(item["rank"]) == rank),
                    None,
                )
                if sampled:
                    _, sampled_wchan, sampled_syscall = top_kernel_wait(sampled)
                    waits.append((sampled_wchan, sampled_syscall))
            stable = sum(wait == waits[0] for wait in waits) if waits else 0
            rows.append(
                {
                    "model": worker["model"],
                    "rank": rank,
                    "npu": worker.get("npu"),
                    "pid": worker["pid"],
                    "python_top": python_top,
                    "native_top": native_top,
                    "main_thread": comm,
                    "wchan": wchan,
                    "syscall": syscall_text,
                    "stable_samples": f"{stable}/{len(waits)}",
                    "classification": classify(
                        wchan, syscall_text, native_text or "", python_top
                    ),
                    "capture_dir": str(capture_dir),
                }
            )
    output.mkdir(parents=True, exist_ok=True)
    atomic_json(output / "rank-summary.json", rows)
    fieldnames = [
        "model",
        "rank",
        "npu",
        "pid",
        "python_top",
        "native_top",
        "main_thread",
        "wchan",
        "syscall",
        "stable_samples",
        "classification",
        "capture_dir",
    ]
    with (output / "rank-summary.csv").open(
        "w", encoding="utf-8", newline=""
    ) as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return rows


def summarize(args: argparse.Namespace) -> int:
    capture_dirs = [Path(value).resolve() for value in args.capture_dir]
    rows = summarize_dirs(capture_dirs, Path(args.output).resolve())
    print(json.dumps(rows, indent=2))
    return 0


def add_target_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--diag-dir", default=DEFAULT_DIAG_DIR)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--expected-workers", type=int, default=8)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    preflight_parser = subparsers.add_parser(
        "preflight", help="check targets and diagnostic permissions"
    )
    add_target_arguments(preflight_parser)
    preflight_parser.set_defaults(func=preflight)

    capture_parser = subparsers.add_parser(
        "capture", help="sample and optionally freeze worker stacks"
    )
    add_target_arguments(capture_parser)
    capture_parser.add_argument("--output", required=True)
    capture_parser.add_argument("--sample-delays-ms", default=DEFAULT_DELAYS_MS)
    capture_parser.add_argument("--freeze", action="store_true")
    capture_parser.set_defaults(func=capture)

    status_parser = subparsers.add_parser("status", help="show current worker states")
    add_target_arguments(status_parser)
    status_parser.set_defaults(func=status)

    resume_parser = subparsers.add_parser(
        "resume", help="resume validated frozen workers"
    )
    add_target_arguments(resume_parser)
    resume_parser.set_defaults(func=resume)

    trace_parser = subparsers.add_parser(
        "trace", help="run a short targeted strace on representative ranks"
    )
    add_target_arguments(trace_parser)
    trace_parser.add_argument("--rank", type=int, action="append", required=True)
    trace_parser.add_argument("--duration", type=float, default=2.0)
    trace_parser.add_argument("--syscalls", default=DEFAULT_TRACE_SYSCALLS)
    trace_parser.add_argument("--output", required=True)
    trace_parser.set_defaults(func=trace)

    summarize_parser = subparsers.add_parser(
        "summarize", help="merge one or more capture directories"
    )
    summarize_parser.add_argument("--capture-dir", action="append", required=True)
    summarize_parser.add_argument("--output", required=True)
    summarize_parser.set_defaults(func=summarize)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if sys.platform != "linux":
        print("ERROR: deadlock_snapshot.py must run on Linux", file=sys.stderr)
        return 2
    try:
        return int(args.func(args))
    except CollectionError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
