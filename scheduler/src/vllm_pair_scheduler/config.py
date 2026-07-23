from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


def _positive_int(env: dict[str, str], name: str, default: int) -> int:
    raw = env.get(name, str(default))
    try:
        value = int(raw)
    except ValueError as exc:
        raise ValueError(f"{name} must be an integer, got {raw!r}") from exc
    if value <= 0:
        raise ValueError(f"{name} must be positive, got {value}")
    return value


@dataclass(frozen=True, slots=True)
class PairSchedulerConfig:
    mode: str = "off"
    role: str | None = None
    instance_id: str | None = None
    pair_id: str | None = None
    shm_dir: Path = Path("/dev/shm/vllm-pair-scheduler")
    init_timeout_ms: int = 30_000
    forward_timeout_ms: int = 30_000
    heartbeat_ms: int = 100
    peer_timeout_ms: int = 1_000

    def __post_init__(self) -> None:
        if self.mode not in {"off", "elastic"}:
            if self.mode == "fix-shared":
                raise ValueError("fix-shared is not implemented in protocol v3")
            raise ValueError(f"unsupported pair scheduler mode: {self.mode!r}")
        if self.mode == "off":
            return
        if self.role not in {"primary", "standby"}:
            raise ValueError("VLLM_PAIR_SCHED_ROLE must be primary or standby")
        if self.instance_id not in {"A", "B"}:
            raise ValueError("VLLM_PAIR_SCHED_INSTANCE_ID must be A or B")
        if not self.pair_id:
            raise ValueError("VLLM_PAIR_SCHED_PAIR_ID is required in elastic mode")
        if self.role == "primary" and self.instance_id != "A":
            raise ValueError("protocol v3 requires primary role on instance A")
        if self.role == "standby" and self.instance_id != "B":
            raise ValueError("protocol v3 requires standby role on instance B")
        for name in (
            "init_timeout_ms",
            "forward_timeout_ms",
            "heartbeat_ms",
            "peer_timeout_ms",
        ):
            if getattr(self, name) <= 0:
                raise ValueError(f"{name} must be positive")
        if self.peer_timeout_ms <= self.heartbeat_ms * 2:
            raise ValueError("peer timeout must exceed two heartbeat periods")

    @classmethod
    def from_env(cls, env: dict[str, str] | None = None) -> PairSchedulerConfig:
        values = os.environ if env is None else env
        mode = values.get("VLLM_PAIR_SCHED_MODE", "off").lower()
        return cls(
            mode=mode,
            role=values.get("VLLM_PAIR_SCHED_ROLE", "").lower() or None,
            instance_id=values.get("VLLM_PAIR_SCHED_INSTANCE_ID", "").upper()
            or None,
            pair_id=values.get("VLLM_PAIR_SCHED_PAIR_ID") or None,
            shm_dir=Path(
                values.get(
                    "VLLM_PAIR_SCHED_SHM_DIR", "/dev/shm/vllm-pair-scheduler"
                )
            ),
            init_timeout_ms=_positive_int(
                values, "VLLM_PAIR_SCHED_INIT_TIMEOUT_MS", 30_000
            ),
            forward_timeout_ms=_positive_int(
                values, "VLLM_PAIR_SCHED_FORWARD_TIMEOUT_MS", 30_000
            ),
            heartbeat_ms=_positive_int(
                values, "VLLM_PAIR_SCHED_HEARTBEAT_MS", 100
            ),
            peer_timeout_ms=_positive_int(
                values, "VLLM_PAIR_SCHED_PEER_TIMEOUT_MS", 1_000
            ),
        )
