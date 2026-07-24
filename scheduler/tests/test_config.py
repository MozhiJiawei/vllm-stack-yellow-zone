from pathlib import Path

import pytest

from vllm_pair_scheduler.config import PairSchedulerConfig


def test_default_is_disabled() -> None:
    assert PairSchedulerConfig.from_install(Path("/missing/role")).mode == "off"


@pytest.mark.parametrize(
    ("role", "instance"), [("primary", "A"), ("standby", "B")]
)
def test_installed_role_selects_fixed_profile(
    tmp_path: Path, role: str, instance: str
) -> None:
    role_file = tmp_path / "role"
    role_file.write_text(f"{role}\n", encoding="ascii")
    config = PairSchedulerConfig.from_install(role_file)
    assert config.mode == "elastic"
    assert config.role == role
    assert config.instance_id == instance
    assert config.pair_id == "default"
    assert config.shm_dir == Path("/dev/shm/vllm-pair-scheduler")
    assert config.forward_timeout_ms == 30_000


def test_invalid_installed_role_is_rejected(tmp_path: Path) -> None:
    role_file = tmp_path / "role"
    role_file.write_text("leader\n", encoding="ascii")
    with pytest.raises(ValueError, match="primary or standby"):
        PairSchedulerConfig.from_install(role_file)


@pytest.mark.parametrize("mode", ["fix-shared", "unknown"])
def test_unsupported_mode(mode: str) -> None:
    with pytest.raises(ValueError):
        PairSchedulerConfig(mode=mode)


def test_role_and_instance_are_fixed_in_v3() -> None:
    with pytest.raises(ValueError, match="instance A"):
        PairSchedulerConfig(
            mode="elastic", role="primary", instance_id="B", pair_id="pair"
        )
