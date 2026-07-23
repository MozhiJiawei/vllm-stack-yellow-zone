from pathlib import Path

import pytest

from vllm_pair_scheduler.config import PairSchedulerConfig


def test_default_is_disabled() -> None:
    assert PairSchedulerConfig.from_env({}).mode == "off"


def test_elastic_config() -> None:
    config = PairSchedulerConfig.from_env(
        {
            "VLLM_PAIR_SCHED_MODE": "elastic",
            "VLLM_PAIR_SCHED_ROLE": "primary",
            "VLLM_PAIR_SCHED_INSTANCE_ID": "A",
            "VLLM_PAIR_SCHED_PAIR_ID": "models-a-b",
            "VLLM_PAIR_SCHED_SHM_DIR": "/tmp/pair",
        }
    )
    assert config.shm_dir == Path("/tmp/pair")
    assert config.forward_timeout_ms == 30_000


@pytest.mark.parametrize("mode", ["fix-shared", "unknown"])
def test_unsupported_mode(mode: str) -> None:
    with pytest.raises(ValueError):
        PairSchedulerConfig(mode=mode)


def test_role_and_instance_are_fixed_in_v3() -> None:
    with pytest.raises(ValueError, match="instance A"):
        PairSchedulerConfig(
            mode="elastic", role="primary", instance_id="B", pair_id="pair"
        )
