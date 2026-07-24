from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

from .gate import PairSchedulerError, _ERR_SIZE, _SNAPSHOT_SIZE, _load_native

GLOBAL_STATES = {
    0: "INITIALIZING",
    1: "RUNNING",
    2: "FAILED",
    3: "SHUTDOWN",
}
ROUND_STATES = {
    0: "OFFLINE",
    1: "IDLE",
    2: "INITIALIZING",
    3: "COLLECTING",
    4: "READY",
    5: "RUNNING",
    6: "COMPLETE",
    7: "DRAINING",
}


def _signed(value: int) -> int:
    return value - (1 << 64) if value >= (1 << 63) else value


def _age_ms(timestamp_ns: int, now_ns: int) -> float | None:
    if timestamp_ns == 0 or timestamp_ns > now_ns:
        return None
    return round((now_ns - timestamp_ns) / 1_000_000, 3)


def _current_path(pair_id: str, shm_dir: Path) -> tuple[int, Path]:
    pair_hash = hashlib.sha256(pair_id.encode()).hexdigest()[:20]
    current = shm_dir / f"{pair_hash}.current"
    lines = current.read_text(encoding="ascii").splitlines()
    if len(lines) != 2:
        raise PairSchedulerError("invalid current generation record")
    epoch = int(lines[0], 16)
    expected = f"{pair_hash}.{epoch:016x}.shm"
    if lines[1] != expected:
        raise PairSchedulerError(
            "current generation filename does not match epoch"
        )
    return epoch, shm_dir / expected


def inspect_pair(pair_id: str, shm_dir: Path) -> dict[str, Any]:
    expected_epoch, shm_path = _current_path(pair_id, shm_dir)
    lib = _load_native()
    values = (ctypes.c_uint64 * _SNAPSHOT_SIZE)()
    error = ctypes.create_string_buffer(_ERR_SIZE)
    count = lib.ps_inspect(
        os.fsencode(shm_path),
        values,
        len(values),
        error,
        len(error),
    )
    if count < 0:
        raise PairSchedulerError(error.value.decode(errors="replace"))
    raw = tuple(values[:count])
    if raw[2] != expected_epoch:
        raise PairSchedulerError("current record and shared epoch do not match")
    now_ns = time.monotonic_ns()
    peer_timeout_ms = raw[10]
    primary_age_ms = _age_ms(raw[8], now_ns)
    stale = primary_age_ms is None or primary_age_ms > peer_timeout_ms
    worker_count = raw[14]
    instances: dict[str, Any] = {}
    worker_base = 16 + 2 * 10
    for index, name in enumerate(("A", "B")):
        base = 16 + index * 10
        ready_mask = raw[base + 3]
        complete_mask = raw[base + 4]
        exit_mask = raw[base + 5]
        online_mask = raw[base + 6]
        expected_mask = raw[base + 7]
        workers: list[dict[str, Any]] = []
        for rank in range(worker_count):
            offset = worker_base + (index * 64 + rank) * 4
            heartbeat_ns = raw[offset + 3]
            workers.append(
                {
                    "rank": rank,
                    "pid": _signed(raw[offset]),
                    "session": raw[offset + 1],
                    "last_seq": raw[offset + 2],
                    "heartbeat_ns": heartbeat_ns,
                    "heartbeat_age_ms": _age_ms(heartbeat_ns, now_ns),
                    "online": bool(online_mask & (1 << rank)),
                    "ready": bool(ready_mask & (1 << rank)),
                    "complete": bool(complete_mask & (1 << rank)),
                }
            )
        instances[name] = {
            "state": ROUND_STATES.get(
                raw[base], f"INVALID({raw[base]})"
            ),
            "forward_seq": raw[base + 1],
            "grant_id": raw[base + 2],
            "ready_mask": ready_mask,
            "complete_mask": complete_mask,
            "exit_mask": exit_mask,
            "registration_mask": online_mask,
            "expected_worker_mask": expected_mask,
            "registration_complete": (
                online_mask == expected_mask
                and all(
                    worker["pid"] > 0
                    and worker["session"] != 0
                    and worker["heartbeat_age_ms"] is not None
                    and worker["heartbeat_age_ms"] <= peer_timeout_ms
                    for worker in workers
                )
            ),
            "publish_ns": raw[base + 8],
            "error": _signed(raw[base + 9]),
            "workers": workers,
        }
    owner_index = _signed(raw[5])
    last_owner_index = _signed(raw[6])
    return {
        "protocol_version": raw[1],
        "epoch": raw[2],
        "state": GLOBAL_STATES.get(raw[3], f"INVALID({raw[3]})"),
        "failure_reason": raw[4],
        "owner": (
            None
            if owner_index == -1
            else (
                ("A", "B")[owner_index]
                if owner_index in (0, 1)
                else f"INVALID({owner_index})"
            )
        ),
        "last_owner": (
            None
            if last_owner_index == -1
            else (
                ("A", "B")[last_owner_index]
                if last_owner_index in (0, 1)
                else f"INVALID({last_owner_index})"
            )
        ),
        "deadline_ns": raw[7],
        "primary_heartbeat_ns": raw[8],
        "primary_heartbeat_age_ms": primary_age_ms,
        "stale": stale,
        "config": {
            "heartbeat_ms": raw[9],
            "peer_timeout_ms": peer_timeout_ms,
            "forward_timeout_ms": raw[11],
        },
        "next_grant_id": raw[12],
        "layout_size": raw[13],
        "worker_count": worker_count,
        "instances": instances,
    }


def exit_code(snapshot: dict[str, Any]) -> int:
    if snapshot["state"] == "FAILED":
        return 2
    if snapshot["state"] != "RUNNING" or snapshot["stale"]:
        return 3
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Read a vLLM pair scheduler generation without joining it"
    )
    parser.add_argument("--pair-id", default="default")
    parser.add_argument(
        "--shm-dir",
        type=Path,
        default=Path("/dev/shm/vllm-pair-scheduler"),
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        snapshot = inspect_pair(args.pair_id, args.shm_dir)
    except (OSError, PairSchedulerError, ValueError) as exc:
        payload = {"state": "ERROR", "error": str(exc)}
        print(
            json.dumps(payload, sort_keys=True)
            if args.json
            else f"ERROR: {exc}",
            file=sys.stderr,
        )
        return 1
    if args.json:
        print(json.dumps(snapshot, sort_keys=True))
    else:
        print(
            f"{snapshot['state']} epoch={snapshot['epoch']:016x} "
            f"owner={snapshot['owner']} reason={snapshot['failure_reason']} "
            f"stale={snapshot['stale']}"
        )
    return exit_code(snapshot)


if __name__ == "__main__":
    raise SystemExit(main())
