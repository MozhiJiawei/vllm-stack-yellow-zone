#!/usr/bin/env bash
set -euo pipefail

ROOT=${ROOT:-/root/l00933108}
PHYSICAL_NPUS=${PHYSICAL_NPUS:-4,5,6,7}
NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
PRIMARY_CONTAINER=${PRIMARY_CONTAINER:-cont1_ljw}
STANDBY_CONTAINER=${STANDBY_CONTAINER:-cont2_ljw}
ARTIFACT_DIR="$ROOT/.artifacts/pair-scheduler"
PATCH="$ROOT/patches/vllm-pair-elastic-scheduling.patch"

usage() {
  cat <<EOF
Usage: bash scripts/pair-scheduler/prepare-yellow-zone.sh [OPTIONS]

Options:
  --root PATH              repository root (default: /root/l00933108)
  --physical-npus LIST     physical NPU list (default: 4,5,6,7)
  --namespace NAME         containerd namespace (default: k8s.io)
  -h, --help               show this help

This command always rebuilds the cleaned vCANN runtime and recreates both
containers through scripts/restart-vcann-xlite-containers.sh.
EOF
}

while (($#)); do
  case "$1" in
    --root)
      ROOT=$2
      ARTIFACT_DIR="$ROOT/.artifacts/pair-scheduler"
      PATCH="$ROOT/patches/vllm-pair-elastic-scheduling.patch"
      shift 2
      ;;
    --physical-npus)
      PHYSICAL_NPUS=$2
      shift 2
      ;;
    --namespace)
      NAMESPACE=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

cd "$ROOT"
test -f "$PATCH"
test -f "$ROOT/scripts/restart-vcann-xlite-containers.sh"

for container in "$PRIMARY_CONTAINER" "$STANDBY_CONTAINER"; do
  if ctr -n "$NAMESPACE" tasks ls 2>/dev/null |
      awk 'NR > 1 {print $1}' | grep -qx "$container"; then
    if ctr -n "$NAMESPACE" tasks exec \
        --exec-id "pair-running-check-$RANDOM" "$container" \
        /bin/bash -lc \
        "pgrep -af 'EngineCore|vllm serve' >/dev/null"; then
      echo "Refusing to rebuild while vLLM is running in $container" >&2
      exit 1
    fi
  fi
done

bash "$ROOT/scripts/restart-vcann-xlite-containers.sh" \
  --restart \
  --physical-npus "$PHYSICAL_NPUS"

mkdir -p "$ARTIFACT_DIR"
rm -f "$ARTIFACT_DIR"/vllm_pair_scheduler-*.whl

ctr -n "$NAMESPACE" tasks exec \
  --exec-id "pair-wheel-$RANDOM" "$PRIMARY_CONTAINER" \
  /bin/bash -lc "
    set -euo pipefail
    command -v gcc
    python - <<'PY'
import setuptools
import wheel
print('python-build-preflight', setuptools.__version__, wheel.__version__)
PY
    python -m pip wheel --no-build-isolation --no-deps \
      '$ROOT/scheduler' -w '$ARTIFACT_DIR'
  "

WHEEL=$(find "$ARTIFACT_DIR" -maxdepth 1 -name 'vllm_pair_scheduler-*.whl' |
  sort | tail -n 1)
test -n "$WHEEL"

for container in "$PRIMARY_CONTAINER" "$STANDBY_CONTAINER"; do
  ctr -n "$NAMESPACE" tasks exec \
    --exec-id "pair-install-$RANDOM" "$container" \
    /bin/bash -lc "
      set -euo pipefail
      test ! -e '$ROOT/patches/vllm-vcann-deterministic-scheduling.patch'
      test ! -e '$ROOT/patches/vllm-ascend-vcann-prefill-guard.patch'
      ! grep -R -E 'rtDetSched|det_sched_|DET_STATE|DET_SNAPSHOT' \
        /vllm-workspace/vllm /vllm-workspace/vllm-ascend \
        --include='*.py' --include='*.cc' --include='*.cpp' --include='*.h'
      command -v nm
      ! nm -D /opt/enpu/vcann-rt/hot/libvruntime.so 2>/dev/null |
        grep -E 'rtDetSched'
      python -m pip install --force-reinstall --no-deps '$WHEEL'
      cd /vllm-workspace/vllm
      git apply --check '$PATCH'
      git apply '$PATCH'
      python -m py_compile vllm/v1/executor/multiproc_executor.py
      ! grep -q 'pair_forward_gate\\|PAIR_SCHED' vllm/v1/engine/core.py
      grep -q '_create_pair_worker_gate' vllm/v1/executor/multiproc_executor.py
      grep -q 'enter_forward' vllm/v1/executor/multiproc_executor.py
      grep -q 'leave_forward' vllm/v1/executor/multiproc_executor.py
      python - <<'PY'
from vllm_pair_scheduler import PairSchedulerConfig
from vllm_pair_scheduler.inspect import main
print('pair-scheduler-import-ok', PairSchedulerConfig().mode, callable(main))
PY
      install -d -m 1770 /dev/shm/vllm-pair-scheduler
    "
done

run_case() {
  local name=$1
  local expect=$2
  local primary_extra=$3
  local standby_extra=$4
  local pair="yellow-preflight-$name-$(date +%s)-$RANDOM"
  local trace="$ARTIFACT_DIR/$name.jsonl"
  rm -f "$trace"

  ctr -n "$NAMESPACE" tasks exec \
    --exec-id "pair-$name-a-$RANDOM" "$PRIMARY_CONTAINER" \
    /bin/bash -lc "
      python '$ROOT/scheduler/tests/fake_engine.py' \
        --role primary --instance A --pair '$pair' \
        --shm-dir /dev/shm/vllm-pair-scheduler \
        --trace '$trace' --forward-timeout-ms 100 $primary_extra
    " &
  local primary_exec=$!
  sleep 0.1
  set +e
  ctr -n "$NAMESPACE" tasks exec \
    --exec-id "pair-$name-b-$RANDOM" "$STANDBY_CONTAINER" \
    /bin/bash -lc "
      python '$ROOT/scheduler/tests/fake_engine.py' \
        --role standby --instance B --pair '$pair' \
        --shm-dir /dev/shm/vllm-pair-scheduler \
        --trace '$trace' --forward-timeout-ms 100 $standby_extra
    "
  local standby_rc=$?
  wait "$primary_exec"
  local primary_rc=$?
  set -e

  if [[ $expect == success ]]; then
    test "$primary_rc" -eq 0
    test "$standby_rc" -eq 0
    python "$ROOT/scheduler/tests/verify_trace.py" "$trace"
  else
    if [[ $primary_rc -eq 0 && $standby_rc -eq 0 ]]; then
      echo "$name unexpectedly succeeded" >&2
      exit 1
    fi
    python "$ROOT/scheduler/tests/verify_trace.py" \
      "$trace" --expect failure
  fi
}

run_standby_death_case() {
  local pair="yellow-preflight-standby-death-$(date +%s)-$RANDOM"
  local trace="$ARTIFACT_DIR/standby-death.jsonl"
  rm -f "$trace"

  ctr -n "$NAMESPACE" tasks exec \
    --exec-id "pair-standby-death-a-$RANDOM" "$PRIMARY_CONTAINER" \
    /bin/bash -lc "
      python '$ROOT/scheduler/tests/fake_engine.py' \
        --role primary --instance A --pair '$pair' \
        --shm-dir /dev/shm/vllm-pair-scheduler \
        --trace '$trace' --iterations 5 --start-delay-ms 300 \
        --forward-timeout-ms 100
    " &
  local primary_exec=$!
  sleep 0.1
  set +e
  ctr -n "$NAMESPACE" tasks exec \
    --exec-id "pair-standby-death-b-$RANDOM" "$STANDBY_CONTAINER" \
    /bin/bash -lc "
      python '$ROOT/scheduler/tests/fake_engine.py' \
        --role standby --instance B --pair '$pair' \
        --shm-dir /dev/shm/vllm-pair-scheduler \
        --trace '$trace' --crash-after-open --forward-timeout-ms 100
    "
  local standby_rc=$?
  wait "$primary_exec"
  local primary_rc=$?
  set -e

  test "$primary_rc" -eq 0
  test "$standby_rc" -ne 0
  python - "$trace" <<'PY'
import json
import sys

records = [
    json.loads(line)
    for line in open(sys.argv[1], encoding="utf-8")
]
assert any(
    record["instance"] == "A" and record["event"] == "closed"
    for record in records
)
assert not any(
    record["instance"] == "A" and record["event"] == "error"
    for record in records
)
print("verified idle standby death does not stop primary")
PY
}

run_case normal success \
  "--iterations 20 --start-delay-ms 300 --linger-ms 300" \
  "--iterations 20 --start-delay-ms 200"
run_case forward-timeout failure \
  "--iterations 1 --hang-first-ms 250" \
  "--iterations 1"
run_case primary-death failure \
  "--crash-after-open" \
  "--iterations 1"
run_standby_death_case

echo "PAIR_SCHEDULER_PREPARED wheel=$WHEEL physical_npus=$PHYSICAL_NPUS"
