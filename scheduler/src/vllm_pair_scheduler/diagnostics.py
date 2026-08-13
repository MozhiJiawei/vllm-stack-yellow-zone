from __future__ import annotations

import logging
import os
import threading
import time
from collections import defaultdict
from collections.abc import Callable
from typing import Any

_LOGGER = logging.getLogger("vllm_pair_scheduler.sampling_diagnostics")
_ENV_NAME = "VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL"


class SamplingDiagnostics:
    def __init__(self, worker_rank: int, interval: int) -> None:
        self.worker_rank = worker_rank
        self.interval = interval
        self._lock = threading.Lock()
        self._durations: dict[str, list[int]] = defaultdict(list)
        self._sample_count = 0

    def call(self, label: str, function: Callable[..., Any], *args: Any, **kwargs: Any):
        started = time.perf_counter_ns()
        try:
            return function(*args, **kwargs)
        finally:
            duration = time.perf_counter_ns() - started
            report = None
            with self._lock:
                self._durations[label].append(duration)
                if label == "sample_tokens":
                    self._sample_count += 1
                    if self._sample_count % self.interval == 0:
                        report = self._take_report()
            if report is not None:
                _LOGGER.warning(
                    "PAIR_SCHED_SAMPLE_DIAG rank=%d window=%d %s",
                    self.worker_rank,
                    self.interval,
                    report,
                )

    def _take_report(self) -> str:
        fields = []
        for label, values in sorted(self._durations.items()):
            fields.append(
                f"{label}_n={len(values)} "
                f"{label}_mean_us={sum(values) / len(values) / 1_000:.2f} "
                f"{label}_max_us={max(values) / 1_000:.2f}"
            )
        self._durations.clear()
        return " ".join(fields)

    def wrap(self, target: Any, attribute: str, label: str) -> None:
        original = getattr(target, attribute, None)
        if original is None or not callable(original):
            return

        def measured(*args: Any, **kwargs: Any):
            return self.call(label, original, *args, **kwargs)

        setattr(target, attribute, measured)


def install_worker_sampling_diagnostics(
    worker_wrapper: Any, worker_rank: int
) -> SamplingDiagnostics | None:
    raw_interval = os.environ.get(_ENV_NAME)
    if not raw_interval:
        return None
    try:
        interval = int(raw_interval)
    except ValueError as exc:
        raise ValueError(f"{_ENV_NAME} must be a positive integer") from exc
    if interval <= 0:
        raise ValueError(f"{_ENV_NAME} must be a positive integer")

    diagnostics = SamplingDiagnostics(worker_rank, interval)
    worker = getattr(worker_wrapper, "worker", worker_wrapper)
    model_runner = getattr(worker, "model_runner", None)
    if model_runner is None:
        _LOGGER.warning(
            "PAIR_SCHED_SAMPLE_DIAG rank=%d model_runner=unavailable", worker_rank
        )
        return diagnostics

    diagnostics.wrap(model_runner, "_sample", "model_runner_sample")
    diagnostics.wrap(
        model_runner,
        "_update_states_after_model_execute",
        "update_states",
    )
    diagnostics.wrap(model_runner, "_bookkeeping_sync", "bookkeeping")

    sampler = getattr(model_runner, "sampler", None)
    if sampler is not None:
        diagnostics.wrap(sampler, "sample", "sampler_core")
        topk_topp_sampler = getattr(sampler, "topk_topp_sampler", None)
        if topk_topp_sampler is not None:
            diagnostics.wrap(
                topk_topp_sampler,
                "apply_top_k_top_p",
                "topk_topp",
            )
    return diagnostics
