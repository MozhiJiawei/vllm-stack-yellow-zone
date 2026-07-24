from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


ROLE_FILE = Path("/etc/vllm-pair-scheduler/role")
SHM_DIR = Path("/dev/shm/vllm-pair-scheduler")
PAIR_ID = "default"


@dataclass(frozen=True, slots=True)
class PairSchedulerConfig:
    mode: str = "off"
    role: str | None = None
    instance_id: str | None = None
    pair_id: str | None = None
    shm_dir: Path = SHM_DIR
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
            raise ValueError("installed role must be primary or standby")
        if self.instance_id not in {"A", "B"}:
            raise ValueError("instance_id must be A or B")
        if not self.pair_id:
            raise ValueError("pair_id is required in elastic mode")
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
    def from_install(
        cls, role_file: Path = ROLE_FILE
    ) -> PairSchedulerConfig:
        """Load the fixed deployment profile written by the installer.

        A missing role file is the sole off switch. All protocol settings are
        deliberately fixed for the first same-host, one-pair deployment.
        """
        try:
            role = role_file.read_text(encoding="ascii").strip().lower()
        except FileNotFoundError:
            return cls()
        if role not in {"primary", "standby"}:
            raise ValueError(
                f"{role_file} must contain exactly primary or standby"
            )
        return cls(
            mode="elastic",
            role=role,
            instance_id="A" if role == "primary" else "B",
            pair_id=PAIR_ID,
        )
