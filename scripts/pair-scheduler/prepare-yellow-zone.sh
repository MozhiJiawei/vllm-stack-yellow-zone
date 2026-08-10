#!/usr/bin/env bash
set -euo pipefail

ROOT=${ROOT:-/root/l00933108/vllm-stack-yellow-zone}
DEPS_ROOT=${DEPS_ROOT:-/root/l00933108/deps}
PHYSICAL_NPUS=${PHYSICAL_NPUS:-4,5,6,7}
NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
CTR_BIN=${CTR_BIN:-/root/l00933108/.tools/containerd/bin/ctr}
IMAGE=${IMAGE:-quay.io/ascend/vllm-ascend:v0.19.1rc1}
XLITE_WHEEL=${XLITE_WHEEL:-}
PRIMARY_CONTAINER=${PRIMARY_CONTAINER:-cont1_ljw}
STANDBY_CONTAINER=${STANDBY_CONTAINER:-cont2_ljw}
ARTIFACT_DIR="$ROOT/.artifacts/pair-scheduler"
PATCH="$ROOT/patches/vllm-pair-elastic-scheduling.patch"

usage() {
  cat <<EOF
Usage: bash scripts/pair-scheduler/prepare-yellow-zone.sh [OPTIONS]

Options:
  --root PATH              repository root (default: $ROOT)
  --deps-root PATH         dependency directory (default: $DEPS_ROOT)
  --ctr PATH               unrestricted ctr client (default: $CTR_BIN)
  --image REF              vLLM Ascend image (default: $IMAGE)
  --xlite-wheel PATH       xLite wheel (default: DEPS_ROOT/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl)
  --physical-npus LIST     physical NPU list (default: 4,5,6,7)
  --namespace NAME         containerd namespace (default: k8s.io)
  -h, --help               show this help

This command recreates two native Ascend containers and installs xLite plus
the pair scheduler. It does not build, mount, or configure vCANN-RT.
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
    --deps-root) DEPS_ROOT=$2; shift 2 ;;
    --ctr) CTR_BIN=$2; shift 2 ;;
    --image) IMAGE=$2; shift 2 ;;
    --xlite-wheel) XLITE_WHEEL=$2; shift 2 ;;
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

XLITE_WHEEL=${XLITE_WHEEL:-$DEPS_ROOT/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl}

cd "$ROOT"
test -f "$PATCH"
test -f "$ROOT/scripts/pair-scheduler/recreate-native-xlite-containers.sh"
test -x "$CTR_BIN"
test -f "$XLITE_WHEEL"

for container in "$PRIMARY_CONTAINER" "$STANDBY_CONTAINER"; do
  if "$CTR_BIN" -n "$NAMESPACE" tasks ls 2>/dev/null |
      awk 'NR > 1 {print $1}' | grep -qx "$container"; then
    if "$CTR_BIN" -n "$NAMESPACE" tasks exec \
        --exec-id "pair-running-check-$RANDOM" "$container" \
        /bin/bash -lc \
        "pgrep -af 'EngineCore|vllm serve' >/dev/null"; then
      echo "Refusing to rebuild while vLLM is running in $container" >&2
      exit 1
    fi
  fi
done

bash "$ROOT/scripts/pair-scheduler/recreate-native-xlite-containers.sh" \
  --root "$ROOT" \
  --deps-root "$DEPS_ROOT" \
  --ctr "$CTR_BIN" \
  --namespace "$NAMESPACE" \
  --image "$IMAGE" \
  --xlite-wheel "$XLITE_WHEEL" \
  --physical-npus "$PHYSICAL_NPUS"

install_role() {
  local container=$1
  local role=$2
  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
    --exec-id "pair-install-$role-$RANDOM" "$container" \
    /bin/bash -lc "
      set -euo pipefail
      bash '$ROOT/scheduler/install-pair-scheduler.sh' '$ROOT' '$role'
      cd /vllm-workspace/vllm
      ! grep -q 'pair_forward_gate\\|PAIR_SCHED' vllm/v1/engine/core.py
      grep -q '_install_pair_worker_gate' vllm/v1/executor/uniproc_executor.py
      grep -q '_install_pair_worker_gate' vllm/v1/executor/multiproc_executor.py
      grep -q 'enter_forward' vllm/v1/executor/multiproc_executor.py
      grep -q 'leave_forward' vllm/v1/executor/multiproc_executor.py
    "
}

mkdir -p "$ARTIFACT_DIR"
install_role "$PRIMARY_CONTAINER" primary
install_role "$STANDBY_CONTAINER" standby

run_case() {
  local name=$1
  local expect=$2
  local primary_extra=$3
  local standby_extra=$4
  local pair="yellow-preflight-$name-$(date +%s)-$RANDOM"
  local trace="$ARTIFACT_DIR/$name.jsonl"
  rm -f "$trace"

  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
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
  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
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

  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
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
  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
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

echo "PAIR_SCHEDULER_PREPARED xlite_wheel=$XLITE_WHEEL physical_npus=$PHYSICAL_NPUS"
