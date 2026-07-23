from __future__ import annotations

import atexit
import ctypes
import fcntl
import hashlib
import logging
import os
import secrets
import threading
import time
from pathlib import Path
from types import TracebackType
from typing import Final

from .config import PairSchedulerConfig

_ERR_SIZE: Final = 512
_MAX_WORKERS: Final = 64
_SNAPSHOT_SIZE: Final = 16 + 2 * 10 + 2 * _MAX_WORKERS * 4
_LOGGER = logging.getLogger("vllm_pair_scheduler")


class PairSchedulerError(RuntimeError):
    pass


class PairSchedulerTimeout(PairSchedulerError):
    pass


class PairSchedulerFailed(PairSchedulerError):
    pass


class DisabledForwardGate:
    enabled = False

    def acquire(self) -> int:
        return 0

    def complete(self, grant_id: int) -> None:
        del grant_id

    def enter_forward(self) -> tuple[int, int]:
        return 0, 0

    def leave_forward(self, forward_seq: int, grant_id: int) -> None:
        del forward_seq, grant_id

    def fail(self, reason: int = 1) -> None:
        del reason

    def close(self) -> None:
        pass


def _load_native() -> ctypes.CDLL:
    package_dir = Path(__file__).resolve().parent
    candidates = sorted(package_dir.glob("_pair_sched_native*.so"))
    if not candidates:
        raise PairSchedulerError(
            "native shared-memory shim is not built; install the scheduler package "
            "on Linux before enabling elastic mode"
        )
    lib = ctypes.CDLL(str(candidates[0]), use_errno=True)
    lib.ps_open.restype = ctypes.c_void_p
    lib.ps_open.argtypes = [
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_uint32,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]
    lib.ps_enter_forward.restype = ctypes.c_int
    lib.ps_enter_forward.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint64),
        ctypes.POINTER(ctypes.c_uint64),
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]
    lib.ps_leave_forward.restype = ctypes.c_int
    lib.ps_leave_forward.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]
    lib.ps_fail.restype = ctypes.c_int
    lib.ps_fail.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.ps_snapshot.restype = ctypes.c_int
    lib.ps_snapshot.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t]
    lib.ps_inspect.restype = ctypes.c_int
    lib.ps_inspect.argtypes = [
        ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_uint64),
        ctypes.c_size_t,
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]
    lib.ps_close.restype = None
    lib.ps_close.argtypes = [ctypes.c_void_p]
    return lib


class SharedMemoryForwardGate:
    enabled = True

    def __init__(
        self,
        config: PairSchedulerConfig,
        *,
        worker_rank: int = 0,
        worker_count: int = 1,
    ):
        if config.mode != "elastic":
            raise ValueError("SharedMemoryForwardGate requires elastic mode")
        if worker_count < 1 or worker_count > _MAX_WORKERS:
            raise ValueError("worker_count must be between 1 and 64")
        if worker_rank < 0 or worker_rank >= worker_count:
            raise ValueError("worker_rank must identify a local worker")
        self.config = config
        self.worker_rank = worker_rank
        self.worker_count = worker_count
        self._lib = _load_native()
        self._ctx: int | None = None
        # WorkerProc has one RPC thread, but shutdown/failure callbacks may race
        # it. Hold this lock across each native call so ctypes can never observe
        # a context that close() has concurrently released.
        self._state_lock = threading.RLock()
        self._active_round: tuple[int, int] | None = None
        self._lock_file = None
        self._epoch: int | None = None
        self._shm_path: Path | None = None
        self._current_path: Path | None = None

        assert config.pair_id is not None
        pair_hash = hashlib.sha256(config.pair_id.encode()).hexdigest()[:20]
        config.shm_dir.mkdir(mode=0o770, parents=True, exist_ok=True)
        lock_path = config.shm_dir / f"{pair_hash}.lock"
        current_path = config.shm_dir / f"{pair_hash}.current"
        self._current_path = current_path

        self._creator = config.role == "primary" and worker_rank == 0
        if self._creator:
            self._lock_file = lock_path.open("a+")
            os.chmod(lock_path, 0o660)
            try:
                fcntl.flock(
                    self._lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB
                )
            except BlockingIOError as exc:
                self._lock_file.close()
                self._lock_file = None
                raise PairSchedulerError(
                    f"another primary owns pair {config.pair_id!r}"
                ) from exc
            tmp_path: Path | None = None
            try:
                epoch = secrets.randbits(63) or 1
                shm_path = config.shm_dir / f"{pair_hash}.{epoch:016x}.shm"
                self._epoch = epoch
                self._shm_path = shm_path
                self._ctx = self._native_open(shm_path, epoch, create=True)
                tmp_path = config.shm_dir / (
                    f".{pair_hash}.current.{os.getpid()}.{secrets.token_hex(4)}"
                )
                tmp_path.write_text(
                    f"{epoch:016x}\n{shm_path.name}\n", encoding="ascii"
                )
                os.chmod(tmp_path, 0o660)
                os.replace(tmp_path, current_path)
                self._remove_stale_generations(pair_hash, keep=shm_path)
            except BaseException:
                if tmp_path is not None:
                    tmp_path.unlink(missing_ok=True)
                self.close()
                raise
        else:
            deadline = time.monotonic() + config.init_timeout_ms / 1000
            last_error: Exception | None = None
            while time.monotonic() < deadline:
                try:
                    epoch, shm_path, record = self._read_current(
                        current_path, pair_hash
                    )
                    self._epoch = epoch
                    self._shm_path = shm_path
                    candidate_ctx = self._native_open(
                        shm_path, epoch, create=False
                    )
                    try:
                        if current_path.read_text(encoding="ascii") != record:
                            raise PairSchedulerError(
                                "current generation changed while standby attached"
                            )
                    except BaseException:
                        self._lib.ps_close(candidate_ctx)
                        raise
                    self._ctx = candidate_ctx
                    break
                except (
                    FileNotFoundError,
                    PairSchedulerError,
                    PermissionError,
                    ValueError,
                ) as exc:
                    self._epoch = None
                    self._shm_path = None
                    last_error = exc
                    time.sleep(min(config.heartbeat_ms / 1000, 0.05))
            if self._ctx is None:
                raise PairSchedulerTimeout(
                    f"primary did not publish a usable generation within "
                    f"{config.init_timeout_ms} ms: {last_error}"
                )
        _LOGGER.info(
            "pair scheduler started pair=%s role=%s instance=%s epoch=%016x",
            config.pair_id,
            config.role,
            config.instance_id,
            self._epoch or 0,
        )
        atexit.register(self.close)

    @staticmethod
    def _read_current(
        current_path: Path, pair_hash: str
    ) -> tuple[int, Path, str]:
        record = current_path.read_text(encoding="ascii")
        lines = record.splitlines()
        if len(lines) != 2:
            raise ValueError("invalid current generation record")
        epoch = int(lines[0], 16)
        expected_name = f"{pair_hash}.{epoch:016x}.shm"
        if lines[1] != expected_name:
            raise ValueError("current generation filename does not match epoch")
        return epoch, current_path.parent / lines[1], record

    def _remove_stale_generations(self, pair_hash: str, *, keep: Path) -> None:
        for candidate in self.config.shm_dir.glob(f"{pair_hash}.*.shm"):
            if candidate != keep:
                candidate.unlink(missing_ok=True)

    def _native_open(self, path: Path, epoch: int, *, create: bool) -> int:
        error = ctypes.create_string_buffer(_ERR_SIZE)
        session = secrets.randbits(63) or 1
        ctx = self._lib.ps_open(
            os.fsencode(path),
            int(create),
            0 if self.config.instance_id == "A" else 1,
            self.worker_rank,
            self.worker_count,
            epoch,
            session,
            self.config.heartbeat_ms,
            self.config.peer_timeout_ms,
            self.config.forward_timeout_ms,
            error,
            len(error),
        )
        if not ctx:
            raise PairSchedulerError(error.value.decode(errors="replace"))
        return ctx

    def _context(self) -> int:
        if self._ctx is None:
            raise PairSchedulerError("gate is closed")
        return self._ctx

    def enter_forward(self) -> tuple[int, int]:
        with self._state_lock:
            ctx = self._context()
            sequence = ctypes.c_uint64()
            grant = ctypes.c_uint64()
            error = ctypes.create_string_buffer(_ERR_SIZE)
            rc = self._lib.ps_enter_forward(
                ctx,
                ctypes.byref(sequence),
                ctypes.byref(grant),
                error,
                len(error),
            )
            if rc == -2:
                raise PairSchedulerTimeout(error.value.decode(errors="replace"))
            if rc != 0:
                raise PairSchedulerFailed(error.value.decode(errors="replace"))
            round_token = (sequence.value, grant.value)
            self._active_round = round_token
        return round_token

    def leave_forward(self, forward_seq: int, grant_id: int) -> None:
        with self._state_lock:
            ctx = self._context()
            error = ctypes.create_string_buffer(_ERR_SIZE)
            rc = self._lib.ps_leave_forward(
                ctx, forward_seq, grant_id, error, len(error)
            )
            if rc != 0:
                raise PairSchedulerFailed(error.value.decode(errors="replace"))
            if self._active_round == (forward_seq, grant_id):
                self._active_round = None

    def acquire(self) -> int:
        """TP1 compatibility wrapper used by the fake-engine harness."""
        _, grant_id = self.enter_forward()
        return grant_id

    def complete(self, grant_id: int) -> None:
        """TP1 compatibility wrapper used by the fake-engine harness."""
        with self._state_lock:
            active = self._active_round
        if active is None or active[1] != grant_id:
            raise PairSchedulerFailed("grant is not active in this worker")
        self.leave_forward(active[0], grant_id)

    def fail(self, reason: int = 1) -> None:
        with self._state_lock:
            try:
                ctx = self._context()
            except PairSchedulerError:
                return
            self._lib.ps_fail(ctx, reason)

    def snapshot(self) -> tuple[int, ...]:
        with self._state_lock:
            ctx = self._context()
            values = (ctypes.c_uint64 * _SNAPSHOT_SIZE)()
            count = self._lib.ps_snapshot(ctx, values, len(values))
            if count < 0:
                raise PairSchedulerError("could not read scheduler snapshot")
            return tuple(values[:count])

    def close(self) -> None:
        with self._state_lock:
            ctx = self._ctx
            self._ctx = None
            if ctx is not None:
                self._lib.ps_close(ctx)
        if self._creator and self._lock_file is not None:
            if self._current_path is not None and self._epoch is not None:
                try:
                    pair_hash = self._current_path.stem
                    current_epoch, current_shm, _ = self._read_current(
                        self._current_path, pair_hash
                    )
                    if current_epoch == self._epoch:
                        self._current_path.unlink(missing_ok=True)
                        current_shm.unlink(missing_ok=True)
                    elif self._shm_path is not None:
                        self._shm_path.unlink(missing_ok=True)
                except (FileNotFoundError, PermissionError, ValueError):
                    if self._shm_path is not None:
                        self._shm_path.unlink(missing_ok=True)
            elif self._shm_path is not None:
                self._shm_path.unlink(missing_ok=True)
            fcntl.flock(self._lock_file.fileno(), fcntl.LOCK_UN)
            self._lock_file.close()
            self._lock_file = None

    def __enter__(self) -> SharedMemoryForwardGate:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        del exc_type, exc, traceback
        self.close()


def create_worker_forward_gate_from_env(
    worker_rank: int,
    worker_count: int,
) -> DisabledForwardGate | SharedMemoryForwardGate:
    config = PairSchedulerConfig.from_env()
    if config.mode == "off":
        return DisabledForwardGate()
    return SharedMemoryForwardGate(
        config, worker_rank=worker_rank, worker_count=worker_count
    )


def create_forward_gate_from_env() -> DisabledForwardGate | SharedMemoryForwardGate:
    """Backward-compatible TP1 factory for local fake-engine tests."""
    return create_worker_forward_gate_from_env(0, 1)
