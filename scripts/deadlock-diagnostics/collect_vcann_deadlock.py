#!/usr/bin/env python3
"""Freeze and inspect processes that have loaded vCANN-RT libvruntime.so.

This collector has no vLLM integration. Targets are either explicit ``--pid``
values or processes whose ``/proc/PID/maps`` contains libvruntime.so.
"""

from __future__ import annotations

import argparse
import csv
import functools
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 2
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
TRACE_MAGIC = 0x5643414E4E545243
TRACE_ABI_VERSION = 3
TRACE_CAPACITY = 4096
TRACE_HEADER = struct.Struct("<QIIIIQ")
TRACE_RECORD = struct.Struct("<QQQQQQIIII")
SYNC_PROBE = struct.Struct("<IIiIQQQ")
HOST_SYNC_PROBE = struct.Struct("<IIIiQQQ")
KERNEL_REGISTRY_MAGIC = 0x5643414E4E4B5247
KERNEL_REGISTRY_ABI_VERSION = 1
KERNEL_REGISTRY_CAPACITY = 512
KERNEL_REGISTRY_HEADER = struct.Struct("<QIIQII")
KERNEL_REGISTRATION = struct.Struct("<QQQQII128s128s")
KIND_NAMES = {
    0: "INVALID", 1: "RT_KERNEL_LAUNCH", 2: "RT_KERNEL_HANDLE",
    3: "RT_KERNEL_HANDLE_V2", 4: "RT_KERNEL_FLAG", 5: "RT_KERNEL_FLAG_V2",
    6: "RT_KERNEL_EX", 7: "RT_KERNEL_FWK", 8: "RT_CPU_KERNEL",
    9: "RT_AICPU_KERNEL", 10: "RT_AICPU_KERNEL_EX", 11: "RT_FUNC_HANDLE",
    12: "RT_FUNC_HANDLE_V2", 13: "RT_FUNC_HANDLE_V3", 14: "RT_VECTOR_HANDLE",
    15: "RT_VECTOR_KERNEL", 16: "RTS_KERNEL_HOST_ARGS", 17: "RTS_CPU_KERNEL",
    18: "RTS_KERNEL_CONFIG", 19: "RTS_KERNEL_DEV_ARGS", 20: "RTS_RANDOM_TASK",
    21: "RTS_REDUCE_TASK", 22: "RTS_UPDATE_TASK", 23: "RT_FFTS_TASK",
    24: "RT_STARS_TASK", 25: "RT_CMO_TASK", 26: "RT_BARRIER_TASK",
    27: "RT_MULTIPLE_TASK", 28: "RT_MODEL_EXECUTE", 29: "RT_MODEL_EXECUTE_SYNC",
    30: "EVENT_RECORD", 31: "EVENT_WAIT", 32: "NOTIFY_RECORD",
    33: "NOTIFY_WAIT", 34: "STREAM_DESTROY", 35: "STREAM_CAPTURE_BEGIN",
    36: "STREAM_CAPTURE_END", 37: "SCHED_SYNC_BEGIN", 38: "SCHED_SYNC_END",
    39: "KERNEL_REGISTER", 40: "KERNEL_UNREGISTER",
    41: "DEVICE_SYNC_BEGIN", 42: "DEVICE_SYNC_END",
    43: "STREAM_SYNC_BEGIN", 44: "STREAM_SYNC_END",
    45: "ACL_KERNEL", 46: "ACL_KERNEL_CONFIG", 47: "ACL_KERNEL_V2",
    48: "ACL_KERNEL_HOST_ARGS",
}
STRING_OBJECT_KINDS = {7, 8, 10}


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


def pointer_text(value: int) -> str:
    return f"0x{value:x}" if value else "0x0"


@functools.lru_cache(maxsize=None)
def gdb_python_supported(gdb: str) -> bool:
    marker = "VCANN_GDB_PYTHON_OK"
    result = run_command(
        [gdb, "-nx", "-q", "-batch", "-ex", f'python print("{marker}")'],
        timeout=15.0,
    )
    return result.get("returncode") == 0 and marker in result.get("stdout", "")


def decode_raw_trace(
    trace_raw: bytes,
    probe_raw: bytes,
    host_probe_raw: bytes,
    registry_raw: bytes,
    limit: int,
    string_reader: Any | None = None,
) -> dict[str, Any]:
    expected_trace_size = (
        TRACE_HEADER.size + TRACE_CAPACITY * 4 + TRACE_CAPACITY * TRACE_RECORD.size
    )
    expected_registry_size = (
        KERNEL_REGISTRY_HEADER.size
        + KERNEL_REGISTRY_CAPACITY * KERNEL_REGISTRATION.size
    )
    if (
        len(trace_raw) != expected_trace_size
        or len(probe_raw) != SYNC_PROBE.size
        or len(host_probe_raw) != HOST_SYNC_PROBE.size
        or len(registry_raw) != expected_registry_size
    ):
        return {
            "available": False,
            "error": "unexpected raw vCANN trace size",
            "observed": {
                "trace": len(trace_raw), "probe": len(probe_raw),
                "host_probe": len(host_probe_raw), "registry": len(registry_raw),
            },
            "expected": {
                "trace": expected_trace_size, "probe": SYNC_PROBE.size,
                "host_probe": HOST_SYNC_PROBE.size, "registry": expected_registry_size,
            },
        }
    magic, abi_version, capacity, enabled, process_id, next_sequence = (
        TRACE_HEADER.unpack_from(trace_raw)
    )
    if (
        magic != TRACE_MAGIC
        or abi_version != TRACE_ABI_VERSION
        or capacity != TRACE_CAPACITY
    ):
        return {
            "available": False,
            "error": "incompatible vCANN trace ABI",
            "observed": {
                "magic": f"0x{magic:016x}",
                "abi_version": abi_version,
                "capacity": capacity,
            },
        }
    active, vnpu_id, owner, schedule_turn, begin_ns, begin_sequence, sync_stream = (
        SYNC_PROBE.unpack(probe_raw)
    )
    if not active:
        sync_stream = 0
    (
        host_active,
        host_kind,
        host_tid,
        host_timeout,
        host_begin_ns,
        host_begin_sequence,
        host_stream,
    ) = HOST_SYNC_PROBE.unpack(host_probe_raw)
    (
        registry_magic,
        registry_abi,
        registry_capacity,
        registry_next_sequence,
        registry_dropped,
        _registry_lock,
    ) = KERNEL_REGISTRY_HEADER.unpack_from(registry_raw)
    registrations = []
    registrations_by_handle: dict[int, list[str]] = {}
    if (
        registry_magic == KERNEL_REGISTRY_MAGIC
        and registry_abi == KERNEL_REGISTRY_ABI_VERSION
        and registry_capacity == KERNEL_REGISTRY_CAPACITY
    ):
        count = min(registry_next_sequence, registry_capacity)
        for index in range(count):
            fields = KERNEL_REGISTRATION.unpack_from(
                registry_raw,
                KERNEL_REGISTRY_HEADER.size + index * KERNEL_REGISTRATION.size,
            )
            committed, handle, stub, device_function, mode, tid, stub_raw, device_raw = fields
            if committed != index + 1:
                continue
            stub_name = stub_raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")
            device_name = device_raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")
            registration = {
                "sequence": committed,
                "handle": pointer_text(handle),
                "stub": pointer_text(stub),
                "device_function": pointer_text(device_function),
                "function_mode": mode,
                "tid": tid,
                "stub_name": stub_name,
                "device_name": device_name,
            }
            registrations.append(registration)
            registrations_by_handle.setdefault(handle, []).append(stub_name)
    records_offset = TRACE_HEADER.size + capacity * 4
    first_sequence = max(1, next_sequence - min(limit, capacity) + 1)
    records = []
    for sequence in range(first_sequence, next_sequence + 1):
        slot = (sequence - 1) % capacity
        fields = TRACE_RECORD.unpack_from(
            trace_raw, records_offset + slot * TRACE_RECORD.size
        )
        (
            committed_sequence,
            timestamp_ns,
            stream_address,
            object_address,
            auxiliary,
            value,
            kind,
            blocks,
            args_size,
            tid,
        ) = fields
        if committed_sequence != sequence:
            continue
        entry = {
            "sequence": sequence,
            "timestamp_ns": timestamp_ns,
            "tid": tid,
            "kind": kind,
            "kind_name": KIND_NAMES.get(kind, f"UNKNOWN_{kind}"),
            "record_phase": (
                "scheduler" if kind in (37, 38)
                else "registration" if kind in (39, 40)
                else "sync" if kind in (41, 42, 43, 44)
                else "hook_attempt"
            ),
            "stream": pointer_text(stream_address),
            "object": pointer_text(object_address),
            "auxiliary": pointer_text(auxiliary),
            "value": value,
            "blocks": blocks,
            "args_size": args_size,
            "on_sync_stream": bool(sync_stream and stream_address == sync_stream),
        }
        if string_reader is not None and kind in STRING_OBJECT_KINDS and object_address:
            name = string_reader(object_address)
            if name:
                entry["object_name"] = name
        kernel_names = registrations_by_handle.get(object_address, [])
        if kernel_names:
            entry["kernel_names"] = sorted(set(kernel_names))
        records.append(entry)
    return {
        "available": True,
        "decoder": "raw-memory",
        "magic": f"0x{magic:016x}",
        "abi_version": abi_version,
        "capacity": capacity,
        "enabled": bool(enabled),
        "process_id": process_id,
        "next_sequence": next_sequence,
        "sync_probe": {
            "active": bool(active),
            "vnpu_id": vnpu_id,
            "owner": owner,
            "schedule_turn": schedule_turn,
            "begin_ns": begin_ns,
            "begin_sequence": begin_sequence,
            "stream": pointer_text(sync_stream),
        },
        "host_sync_probe": {
            "active": bool(host_active),
            "kind": host_kind,
            "kind_name": KIND_NAMES.get(host_kind, f"UNKNOWN_{host_kind}"),
            "tid": host_tid,
            "timeout": host_timeout,
            "begin_ns": host_begin_ns,
            "begin_sequence": host_begin_sequence,
            "stream": pointer_text(host_stream),
        },
        "kernel_registry": {
            "available": (
                registry_magic == KERNEL_REGISTRY_MAGIC
                and registry_abi == KERNEL_REGISTRY_ABI_VERSION
                and registry_capacity == KERNEL_REGISTRY_CAPACITY
            ),
            "magic": f"0x{registry_magic:016x}",
            "abi_version": registry_abi,
            "capacity": registry_capacity,
            "next_sequence": registry_next_sequence,
            "dropped": registry_dropped,
            "registrations": registrations,
        },
        "runtime_state": {"unavailable": "GDB was built without Python support"},
        "records": records,
    }


def process_string_reader(pid: int) -> tuple[int, Any]:
    memory = os.open(f"/proc/{pid}/mem", os.O_RDONLY)

    def read_string(address: int) -> str:
        try:
            raw = os.pread(memory, 160, address).split(b"\0", 1)[0]
            return raw.decode("utf-8", errors="replace")
        except OSError:
            return ""

    return memory, read_string


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


def build_gdb_capture_command(
    gdb: str, pid: int, output: Path, trace_limit: int, python_supported: bool
) -> list[str]:
    trace_path = output / "vcann-trace.json"
    trace_raw = output / "vcann-trace.raw"
    probe_raw = output / "vcann-sync-probe.raw"
    host_probe_raw = output / "vcann-host-sync-probe.raw"
    registry_raw = output / "vcann-kernel-registry.raw"
    trace_raw_size = (
        TRACE_HEADER.size + TRACE_CAPACITY * 4 + TRACE_CAPACITY * TRACE_RECORD.size
    )
    registry_raw_size = (
        KERNEL_REGISTRY_HEADER.size
        + KERNEL_REGISTRY_CAPACITY * KERNEL_REGISTRATION.size
    )
    command = [gdb, "-nx", "-q", "-batch"]
    if python_supported and GDB_HELPER.exists():
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
    if python_supported and GDB_HELPER.exists():
        command.extend(["-ex", f"vcann-trace-dump {trace_path} {trace_limit}"])
    else:
        command.extend(
            [
                "-ex",
                f"dump binary memory {trace_raw} &g_vcann_trace "
                f"((char *)&g_vcann_trace + {trace_raw_size})",
                "-ex",
                f"dump binary memory {probe_raw} &g_vcann_sync_probe "
                f"((char *)&g_vcann_sync_probe + {SYNC_PROBE.size})",
                "-ex",
                f"dump binary memory {host_probe_raw} &g_vcann_host_sync_probe "
                f"((char *)&g_vcann_host_sync_probe + {HOST_SYNC_PROBE.size})",
                "-ex",
                f"dump binary memory {registry_raw} &g_vcann_kernel_registry "
                f"((char *)&g_vcann_kernel_registry + {registry_raw_size})",
            ]
        )
    command.extend(["-ex", "detach"])
    return command


def collect_gdb(target: dict[str, Any], output: Path, trace_limit: int) -> dict[str, Any]:
    pid = int(target["pid"])
    gdb = shutil.which("gdb")
    if not gdb:
        return {"error": "gdb is not installed"}
    python_supported = gdb_python_supported(gdb)
    trace_path = output / "vcann-trace.json"
    trace_raw = output / "vcann-trace.raw"
    probe_raw = output / "vcann-sync-probe.raw"
    host_probe_raw = output / "vcann-host-sync-probe.raw"
    registry_raw = output / "vcann-kernel-registry.raw"
    command = build_gdb_capture_command(gdb, pid, output, trace_limit, python_supported)
    result = run_command(command)
    write_command(output / "gdb-native.txt", result)
    validate_target(target)
    os.kill(pid, signal.SIGSTOP)
    wait_stopped([target])
    if (
        not python_supported
        and trace_raw.exists()
        and probe_raw.exists()
        and host_probe_raw.exists()
        and registry_raw.exists()
    ):
        memory = None
        try:
            memory, read_string = process_string_reader(pid)
            trace = decode_raw_trace(
                trace_raw.read_bytes(), probe_raw.read_bytes(), host_probe_raw.read_bytes(),
                registry_raw.read_bytes(), trace_limit, read_string
            )
        except OSError as error:
            trace = decode_raw_trace(
                trace_raw.read_bytes(), probe_raw.read_bytes(), host_probe_raw.read_bytes(),
                registry_raw.read_bytes(), trace_limit
            )
            trace["string_read_error"] = str(error)
        finally:
            if memory is not None:
                os.close(memory)
        atomic_json(trace_path, trace)
    summary = {
        key: result.get(key) for key in ("returncode", "error") if key in result
    } | {
        "gdb_python_supported": python_supported,
        "trace_decoder": "gdb-python" if python_supported else "raw-memory",
        "trace_created": trace_path.exists(),
    }
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


def trace_summary(path: Path, qwen_layers: int = 64) -> dict[str, Any]:
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
    annotate_qwen_states(records, qwen_layers)
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
            record.get("kernel_name") or record.get("object_name")
            or record.get("object_symbol") or record.get("object", "")
        )

    blocked = blocked_kernel_candidate(trace, records)

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
        "blocked_kernel": blocked.get("kernel_name", ""),
        "blocked_layer": blocked.get("qwen_layer"),
        "blocked_phase": blocked.get("qwen_phase", ""),
        "blocked_sequence": blocked.get("sequence"),
        "blocked_state_basis": blocked.get("qwen_state_basis", ""),
        "unresolved_kernel_count": blocked.get("unresolved_kernel_count", 0),
        "unresolved_kernel_window": blocked.get("unresolved_kernel_window", ""),
        "unresolved_collectives": blocked.get("unresolved_collectives", ""),
        "completion_evidence": blocked.get("completion_evidence", ""),
    }


def normalize_kernel_name(name: str) -> str:
    value = name.removeprefix(".ascend.meta.").removeprefix("aclrtlaunch_")
    value = value.removesuffix("__")
    return re.sub(r"_\d+_(?:mix_)?(?:aic|aiv)$", "", value)


def record_kernel_name(record: dict[str, Any]) -> str:
    names = [normalize_kernel_name(str(name)) for name in record.get("kernel_names", [])]
    names = sorted({name for name in names if name})
    return "+".join(names)


def annotate_qwen_states(records: list[dict[str, Any]], layers: int) -> None:
    states: dict[str, dict[str, Any]] = {}
    basis = "trace_start"
    kernel_kinds = {
        "RT_KERNEL_LAUNCH", "RT_KERNEL_HANDLE", "RT_KERNEL_HANDLE_V2",
        "RT_KERNEL_FLAG", "RT_KERNEL_FLAG_V2", "RT_FUNC_HANDLE",
        "RT_FUNC_HANDLE_V2", "RT_FUNC_HANDLE_V3", "RT_VECTOR_HANDLE",
        "RT_VECTOR_KERNEL", "RTS_KERNEL_HOST_ARGS", "RTS_KERNEL_CONFIG",
        "RTS_KERNEL_DEV_ARGS", "ACL_KERNEL", "ACL_KERNEL_CONFIG", "ACL_KERNEL_V2",
        "ACL_KERNEL_HOST_ARGS",
    }
    for record in records:
        if record.get("kind_name") == "DEVICE_SYNC_END":
            states.clear()
            basis = "after_completed_device_sync"
            continue
        if record.get("kind_name") not in kernel_kinds:
            continue
        kernel = record_kernel_name(record)
        if not kernel:
            continue
        record["kernel_name"] = kernel
        stream = str(record.get("stream", "0x0"))
        state = states.setdefault(
            stream,
            {"layer": 0, "segment": "attention", "attn_matmuls": 0, "mlp_matmuls": 0},
        )
        record["qwen_layer"] = state["layer"]
        record["qwen_state_basis"] = basis
        if "allreduce" in kernel:
            if state["segment"] == "attention":
                record["qwen_phase"] = "ATTENTION_TP_ALLREDUCE"
                state["segment"] = "mlp"
            else:
                record["qwen_phase"] = "MLP_TP_ALLREDUCE"
                state["layer"] = (state["layer"] + 1) % layers
                state["segment"] = "attention"
                state["attn_matmuls"] = 0
                state["mlp_matmuls"] = 0
        elif "flash_attention" in kernel or kernel.startswith("attention_"):
            record["qwen_phase"] = "ATTENTION"
        elif "matmul" in kernel:
            if state["segment"] == "attention":
                record["qwen_phase"] = (
                    "ATTENTION_QKV" if state["attn_matmuls"] == 0 else "ATTENTION_O"
                )
                state["attn_matmuls"] += 1
            else:
                record["qwen_phase"] = (
                    "MLP_UP_GATE" if state["mlp_matmuls"] == 0 else "MLP_DOWN"
                )
                state["mlp_matmuls"] += 1
        elif "rope" in kernel:
            record["qwen_phase"] = "ATTENTION_ROPE_CACHE"
        elif "silu" in kernel:
            record["qwen_phase"] = "MLP_ACTIVATION"
        elif "rmsnorm" in kernel or kernel.startswith("norm_"):
            record["qwen_phase"] = "NORM"


def blocked_kernel_candidate(
    trace: dict[str, Any], records: list[dict[str, Any]]
) -> dict[str, Any]:
    def describe(candidates: list[dict[str, Any]]) -> dict[str, Any]:
        def description(record: dict[str, Any]) -> str:
            return ":".join(
                str(value) for value in (
                    record.get("sequence", ""), record.get("kernel_name", ""),
                    record.get("qwen_layer", "?"), record.get("qwen_phase", "UNKNOWN"),
                )
            )

        def bounded(items: list[dict[str, Any]]) -> str:
            if len(items) <= 32:
                return ";".join(description(item) for item in items)
            omitted = len(items) - 32
            return ";".join(
                [*(description(item) for item in items[:16]), f"...{omitted} omitted...",
                 *(description(item) for item in items[-16:])]
            )

        collectives = [
            record for record in candidates
            if "allreduce" in str(record.get("kernel_name", "")).lower()
        ]
        return {
            "unresolved_kernel_count": len(candidates),
            "unresolved_kernel_window": bounded(candidates),
            "unresolved_collectives": bounded(collectives),
        }

    probe = trace.get("sync_probe", {})
    if probe.get("active"):
        stream = str(probe.get("stream", ""))
        begin = int(probe.get("begin_sequence") or 0)
        prior_boundaries = [
            record for record in records
            if record.get("kind_name") == "SCHED_SYNC_END"
            and record.get("stream") == stream
            and int(record.get("sequence") or 0) < begin
        ]
        boundary = int(prior_boundaries[-1]["sequence"]) if prior_boundaries else 0
        candidates = [
            record for record in records
            if record.get("stream") == stream
            and boundary < int(record.get("sequence") or 0) < begin
            and record.get("kernel_name")
        ]
        if candidates:
            result = dict(candidates[0])
            result.update(describe(candidates))
            result["completion_evidence"] = (
                "unresolved window starts after the last successful scheduler stream sync; "
                "the first kernel is a boundary, not proof that it is still executing"
                if boundary else "trace contains no prior successful scheduler sync; "
                "the first visible kernel is only the unresolved-window boundary"
            )
            return result
    host_probe = trace.get("host_sync_probe", {})
    if host_probe.get("active"):
        begin = int(host_probe.get("begin_sequence") or 0)
        completed_syncs = [
            int(record.get("sequence") or 0) for record in records
            if record.get("kind_name") == "DEVICE_SYNC_END"
            and int(record.get("sequence") or 0) < begin
        ]
        boundary = completed_syncs[-1] if completed_syncs else 0
        candidates = [
            record for record in records
            if boundary < int(record.get("sequence") or 0) < begin
            and record.get("kernel_name")
        ]
        if candidates:
            result = dict(candidates[-1])
            result.update(describe(candidates))
            result["completion_evidence"] = (
                "window follows the last completed device sync; reported kernel is last submitted "
                "only because device sync provides no per-stream completion boundary"
            )
            return result
    return {}


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
            row.update(
                trace_summary(
                    directory / "vcann-trace.json", int(manifest.get("qwen_layers") or 64)
                )
            )
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
    gdb = shutil.which("gdb")
    checks = {
        "platform": sys.platform,
        "uid": os.getuid(),
        "gdb": gdb,
        "gdb_python_supported": gdb_python_supported(gdb) if gdb else False,
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
    if args.qwen_layers <= 0:
        raise CollectionError("qwen layer count must be positive")
    if not shutil.which("gdb"):
        raise CollectionError("gdb is not installed")
    if not GDB_HELPER.exists():
        raise CollectionError(f"GDB helper is missing: {GDB_HELPER}")
    targets = select_targets(args.pid, args.expected_processes, args.include_no_device)
    atomic_json(
        output / "targets.json",
        {
            "schema_version": SCHEMA_VERSION,
            "model": args.model,
            "qwen_layers": args.qwen_layers,
            "targets": targets,
        },
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
    capture.add_argument("--qwen-layers", type=int, default=64)
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
