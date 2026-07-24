#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <source-root> <primary|standby> [vllm-source]" >&2
  echo "Example: $0 /root/l00933108 primary" >&2
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

SOURCE_ROOT=$(readlink -f "$1")
ROLE=$2
VLLM_SOURCE=$(readlink -f "${3:-/vllm-workspace/vllm}")
SCHEDULER_SOURCE="$SOURCE_ROOT/scheduler"
PATCH_FILE="$SOURCE_ROOT/patches/vllm-pair-elastic-scheduling.patch"
SHM_DIR=/dev/shm/vllm-pair-scheduler
CONFIG_DIR=/etc/vllm-pair-scheduler
ROLE_FILE="$CONFIG_DIR/role"
PRIMARY_MARKER="$SHM_DIR/.primary-installed"

if [[ $ROLE != primary && $ROLE != standby ]]; then
  usage
  exit 2
fi
if [[ $(id -u) -ne 0 ]]; then
  echo "ERROR: run this installer as root inside the vLLM container" >&2
  exit 1
fi

test -f "$SCHEDULER_SOURCE/pyproject.toml"
test -f "$PATCH_FILE"
test -f "$VLLM_SOURCE/vllm/v1/executor/uniproc_executor.py"
test -f "$VLLM_SOURCE/vllm/v1/executor/multiproc_executor.py"
command -v gcc >/dev/null
command -v git >/dev/null
command -v python >/dev/null
command -v pgrep >/dev/null

if pgrep -af 'EngineCore|vllm serve|VLLM::Worker' >/dev/null; then
  echo "ERROR: stop all vLLM processes before installing" >&2
  exit 1
fi

BUILD_DIR=$(mktemp -d)
cleanup() {
  rm -rf -- "$BUILD_DIR"
}
trap cleanup EXIT

python -m pip wheel --no-build-isolation --no-deps \
  "$SCHEDULER_SOURCE" --wheel-dir "$BUILD_DIR"
WHEEL=$(find "$BUILD_DIR" -maxdepth 1 -name 'vllm_pair_scheduler-*.whl' |
  sort | tail -n 1)
test -n "$WHEEL"
python -m pip install --force-reinstall --no-deps "$WHEEL"

cd "$VLLM_SOURCE"
PAIR_FILES=(
  vllm/v1/executor/uniproc_executor.py
  vllm/v1/executor/multiproc_executor.py
)

PATCHED=true
for file in "${PAIR_FILES[@]}"; do
  if ! grep -q 'create_worker_forward_gate_from_install' "$file" ||
      ! grep -q '/etc/vllm-pair-scheduler/role' "$file"; then
    PATCHED=false
  fi
done

if $PATCHED; then
  echo "vLLM patch already installed"
else
  if grep -q '_install_pair_worker_gate' "${PAIR_FILES[@]}"; then
    python - "${PAIR_FILES[@]}" <<'PY'
from pathlib import Path
import sys

replacements = {
    '"""Install the gate only when enabled, leaving MODE=off\'s hot path native."""':
        '"""Install only when a role file exists; otherwise keep the hot path native."""',
    'if os.environ.get("VLLM_PAIR_SCHED_MODE", "off").lower() == "off":':
        'if not os.path.isfile("/etc/vllm-pair-scheduler/role"):',
    "create_worker_forward_gate_from_env":
        "create_worker_forward_gate_from_install",
}
for name in sys.argv[1:]:
    path = Path(name)
    text = path.read_text(encoding="utf-8")
    for old, new in replacements.items():
        if old not in text:
            raise SystemExit(f"ERROR: cannot migrate unknown scheduler patch in {path}")
        text = text.replace(old, new)
    path.write_text(text, encoding="utf-8")
PY
  else
    git -C / apply --no-index --ignore-space-change \
      --directory="${VLLM_SOURCE#/}" --check "$PATCH_FILE"
    git -C / apply --no-index --ignore-space-change \
      --directory="${VLLM_SOURCE#/}" "$PATCH_FILE"
  fi
fi

python -m py_compile "${PAIR_FILES[@]}"

install -d -m 1770 "$SHM_DIR"
install -d -m 0755 "$CONFIG_DIR"
if [[ $ROLE == primary ]]; then
  printf 'protocol=3\n' >"$PRIMARY_MARKER.tmp"
  chmod 0660 "$PRIMARY_MARKER.tmp"
  mv -f "$PRIMARY_MARKER.tmp" "$PRIMARY_MARKER"
elif [[ ! -f $PRIMARY_MARKER ]] ||
    ! grep -qx 'protocol=3' "$PRIMARY_MARKER"; then
  echo "ERROR: primary install marker is not visible in $SHM_DIR" >&2
  echo "Mount the same host directory into both containers and install primary first." >&2
  exit 1
fi

printf '%s\n' "$ROLE" >"$ROLE_FILE.tmp"
chmod 0644 "$ROLE_FILE.tmp"
mv -f "$ROLE_FILE.tmp" "$ROLE_FILE"

python - <<'PY'
from vllm_pair_scheduler import PairSchedulerConfig

config = PairSchedulerConfig.from_install()
assert config.mode == "elastic"
print(
    "PAIR_SCHEDULER_INSTALLED",
    f"role={config.role}",
    f"instance={config.instance_id}",
    f"pair={config.pair_id}",
    f"shm={config.shm_dir}",
)
PY

echo "Start vLLM normally; no VLLM_PAIR_SCHED_* environment variables are required."
