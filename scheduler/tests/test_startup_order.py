from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(os.name != "posix", reason="bash startup test")


def _script() -> Path:
    candidate = (
        Path(__file__).resolve().parents[2]
        / "scripts/pair-scheduler/start-yellow-zone.sh"
    )
    if not candidate.is_file():
        pytest.skip("repository startup script is not mounted")
    return candidate


def _fake_ctr(tmp_path: Path) -> Path:
    fake = tmp_path / "fake-ctr"
    fake.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
args="$*"
state=${FAKE_CTR_STATE:?}
log="$state/events"
mkdir -p "$state"
if [[ "$args" == *pair-start-A-* ]]; then
  [[ ${FAKE_A_START_FAIL:-0} != 1 ]]
  echo start-A >> "$log"
elif [[ "$args" == *pair-ready-A-* ]]; then
  count=$(($(cat "$state/a-count" 2>/dev/null || echo 0) + 1))
  echo "$count" > "$state/a-count"
  if (( count >= ${FAKE_A_READY_AFTER:-3} )); then
    echo ready-A >> "$log"
  else
    exit 1
  fi
elif [[ "$args" == *pair-start-B-* ]]; then
  grep -qx ready-A "$log"
  echo start-B >> "$log"
elif [[ "$args" == *pair-ready-B-* ]]; then
  count=$(($(cat "$state/b-count" 2>/dev/null || echo 0) + 1))
  echo "$count" > "$state/b-count"
  if (( count >= ${FAKE_B_READY_AFTER:-2} )); then
    echo ready-B >> "$log"
  else
    exit 1
  fi
else
  echo "unexpected ctr call: $args" >&2
  exit 2
fi
""",
        encoding="utf-8",
    )
    fake.chmod(0o755)
    return fake


def _run(tmp_path: Path, **extra: str) -> subprocess.CompletedProcess[str]:
    fake = _fake_ctr(tmp_path)
    environment = os.environ.copy()
    environment.update(
        {
            "CTR_BIN": str(fake),
            "FAKE_CTR_STATE": str(tmp_path / "state"),
            "STARTUP_TIMEOUT_SECONDS": "2",
            "STARTUP_POLL_SECONDS": "0.01",
        }
    )
    environment.update(extra)
    return subprocess.run(
        ["bash", str(_script())],
        env=environment,
        text=True,
        capture_output=True,
        timeout=10,
        check=False,
    )


def test_slow_primary_registration_blocks_standby_start(tmp_path: Path) -> None:
    result = _run(tmp_path, FAKE_A_READY_AFTER="4", FAKE_B_READY_AFTER="2")
    assert result.returncode == 0, result.stderr
    events = (tmp_path / "state/events").read_text().splitlines()
    assert events == ["start-A", "ready-A", "start-B", "ready-B"]
    assert "PAIR_READY" in result.stdout


def test_primary_start_failure_never_starts_standby(tmp_path: Path) -> None:
    result = _run(tmp_path, FAKE_A_START_FAIL="1")
    assert result.returncode != 0
    events_path = tmp_path / "state/events"
    events = events_path.read_text().splitlines() if events_path.exists() else []
    assert "start-B" not in events


def test_incomplete_registration_times_out_before_standby(tmp_path: Path) -> None:
    result = _run(tmp_path, FAKE_A_READY_AFTER="1000000")
    assert result.returncode != 0
    events = (tmp_path / "state/events").read_text().splitlines()
    assert events == ["start-A"]
    assert "PAIR_INSTANCE_TIMEOUT instance=A" in result.stderr
