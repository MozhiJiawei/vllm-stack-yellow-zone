from __future__ import annotations

from types import SimpleNamespace

import pytest

from vllm_pair_scheduler.diagnostics import install_worker_sampling_diagnostics


class FakeTopKTopP:
    def apply_top_k_top_p(self, value):
        return value + 1


class FakeSampler:
    def __init__(self) -> None:
        self.topk_topp_sampler = FakeTopKTopP()

    def sample(self, value):
        return self.topk_topp_sampler.apply_top_k_top_p(value)


class FakeRunner:
    def __init__(self) -> None:
        self.sampler = FakeSampler()

    def _sample(self, value):
        return self.sampler.sample(value)

    def _update_states_after_model_execute(self, value):
        return value

    def _bookkeeping_sync(self, value):
        return value


def test_diagnostics_are_not_installed_by_default(monkeypatch) -> None:
    monkeypatch.delenv("VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL", raising=False)
    runner = FakeRunner()
    wrapper = SimpleNamespace(worker=SimpleNamespace(model_runner=runner))

    assert install_worker_sampling_diagnostics(wrapper, 0) is None
    assert runner._sample(1) == 2


def test_diagnostics_measure_nested_sampling_phases(monkeypatch, caplog) -> None:
    monkeypatch.setenv("VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL", "2")
    runner = FakeRunner()
    wrapper = SimpleNamespace(worker=SimpleNamespace(model_runner=runner))
    diagnostics = install_worker_sampling_diagnostics(wrapper, 3)
    assert diagnostics is not None

    for _ in range(2):
        diagnostics.call("sample_tokens", runner._sample, 1)
        runner._update_states_after_model_execute(1)
        runner._bookkeeping_sync(1)

    message = caplog.records[-1].getMessage()
    assert "PAIR_SCHED_SAMPLE_DIAG rank=3 window=2" in message
    for label in (
        "sample_tokens",
        "model_runner_sample",
        "sampler_core",
        "topk_topp",
    ):
        assert f"{label}_n=2" in message


@pytest.mark.parametrize("value", ["0", "-1", "invalid"])
def test_diagnostics_reject_invalid_intervals(monkeypatch, value) -> None:
    monkeypatch.setenv("VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL", value)
    with pytest.raises(ValueError, match="must be a positive integer"):
        install_worker_sampling_diagnostics(SimpleNamespace(), 0)
