#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=${ROOT:-/root/l00933108/vllm-stack-yellow-zone}
DEPS_ROOT=${DEPS_ROOT:-/root/l00933108/deps}
CTR_BIN=${CTR_BIN:-/root/l00933108/.tools/containerd/bin/ctr}
NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
IMAGE=${IMAGE:-quay.io/ascend/vllm-ascend:v0.19.1rc1}
XLITE_WHEEL=${XLITE_WHEEL:-}
XLITE_SHA256=${XLITE_SHA256:-cccb74688f6acb9cc219290c3a04b6005b81dba941b9d63c79bd52d02854fc8a}
XLITE_EXPECTED_VERSION=${XLITE_EXPECTED_VERSION:-0.1.0rc12}
MODEL_ROOT=${MODEL_ROOT:-/cache/models}
SHM_ROOT=${SHM_ROOT:-/opt/l00933108}
TOOLS_ROOT=${TOOLS_ROOT:-/root/l00933108/.tools}
PHYSICAL_NPUS=${PHYSICAL_NPUS:-4,5,6,7}
PRIMARY_CONTAINER=${PRIMARY_CONTAINER:-cont1_ljw}
STANDBY_CONTAINER=${STANDBY_CONTAINER:-cont2_ljw}

usage() {
  cat <<EOF
Usage: bash scripts/pair-scheduler/recreate-native-xlite-containers.sh [OPTIONS]

Options:
  --root PATH              repository root (default: $ROOT)
  --deps-root PATH         dependency directory (default: $DEPS_ROOT)
  --ctr PATH               unrestricted ctr client (default: $CTR_BIN)
  --namespace NAME         containerd namespace (default: $NAMESPACE)
  --image REF              vLLM Ascend image (default: $IMAGE)
  --xlite-wheel PATH       xLite wheel (default: DEPS_ROOT/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl)
  --model-root PATH        host model directory (default: $MODEL_ROOT)
  --physical-npus LIST     physical NPU list (default: $PHYSICAL_NPUS)
  -h, --help               show this help

This entry point creates two native Ascend containers. It deliberately does
not mount or configure vCANN-RT, npu_info.config, ld.so.preload, GDB, or
enpu-monitor.
EOF
}

while (($#)); do
  case "$1" in
    --root) ROOT=$2; shift 2 ;;
    --deps-root) DEPS_ROOT=$2; shift 2 ;;
    --ctr) CTR_BIN=$2; shift 2 ;;
    --namespace) NAMESPACE=$2; shift 2 ;;
    --image) IMAGE=$2; shift 2 ;;
    --xlite-wheel) XLITE_WHEEL=$2; shift 2 ;;
    --model-root) MODEL_ROOT=$2; shift 2 ;;
    --physical-npus) PHYSICAL_NPUS=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

XLITE_WHEEL=${XLITE_WHEEL:-$DEPS_ROOT/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

require_path() {
  [[ -e $1 ]] || fail "required path not found: $1"
}

task_field() {
  local container=$1
  local field=$2
  "$CTR_BIN" -n "$NAMESPACE" tasks list |
    awk -v container="$container" -v field="$field" \
      'NR > 1 && $1 == container {print $field; found=1} END {exit !found}'
}

container_exec() {
  local container=$1
  shift
  local pid
  local -a container_env=()
  pid=$(task_field "$container" 2) || fail "container task not found: $container"
  [[ $pid =~ ^[0-9]+$ ]] || fail "invalid task PID for $container: $pid"
  while IFS= read -r -d '' item; do
    container_env+=("$item")
  done <"/proc/$pid/environ"
  nsenter --target "$pid" --mount --uts --ipc --net --pid \
    --root="/proc/$pid/root" --wd="/proc/$pid/root" -- \
    /usr/bin/env -i "${container_env[@]}" "$@"
}

delete_exact_container() {
  local container=$1
  "$CTR_BIN" -n "$NAMESPACE" tasks delete --force "$container" \
    >/dev/null 2>&1 || true
  "$CTR_BIN" -n "$NAMESPACE" containers delete "$container" \
    >/dev/null 2>&1 || true
}

[[ $EUID -eq 0 ]] || fail 'run as root on the containerd host'
[[ -x $CTR_BIN ]] || fail "unrestricted ctr client is missing: $CTR_BIN"
command -v nsenter >/dev/null || fail 'required host command not found: nsenter'
require_path "$ROOT/scheduler/install-pair-scheduler.sh"
require_path "$ROOT/patches/vllm-pair-elastic-scheduling.patch"
require_path "$XLITE_WHEEL"
require_path "$MODEL_ROOT/Qwen3-4B"
require_path "$TOOLS_ROOT/git/bin/git"
require_path /usr/local/Ascend/driver
require_path /usr/local/dcmi
require_path /usr/local/sbin/npu-smi
require_path /etc/ascend_install.info
require_path /dev/davinci_manager
require_path /dev/devmm_svm
require_path /dev/hisi_hdc

actual_xlite_sha256=$(sha256sum "$XLITE_WHEEL" | awk '{print $1}')
[[ $actual_xlite_sha256 == "$XLITE_SHA256" ]] ||
  fail "xLite wheel SHA-256 mismatch: $actual_xlite_sha256"

"$CTR_BIN" -n "$NAMESPACE" images list -q | grep -Fx -- "$IMAGE" >/dev/null ||
  fail "image is not present in namespace $NAMESPACE: $IMAGE"

IFS=, read -r -a physical_npu_ids <<<"$PHYSICAL_NPUS"
(( ${#physical_npu_ids[@]} > 0 )) || fail 'physical NPU list is empty'
declare -a device_args=()
declare -A seen=()
for physical_id in "${physical_npu_ids[@]}"; do
  [[ $physical_id =~ ^[0-7]$ ]] || fail "invalid physical NPU ID: $physical_id"
  [[ -z ${seen[$physical_id]:-} ]] || fail "duplicate physical NPU ID: $physical_id"
  seen[$physical_id]=1
  require_path "/dev/davinci$physical_id"
  device_args+=(--device "/dev/davinci$physical_id")
done

for container in "$PRIMARY_CONTAINER" "$STANDBY_CONTAINER"; do
  if status=$(task_field "$container" 3 2>/dev/null); then
    [[ $status != RUNNING ]] || {
      if container_exec "$container" /bin/bash -lc \
          "pgrep -af '[E]ngineCore|[v]llm serve|VLLM::[W]orker' >/dev/null"; then
        fail "refusing to replace $container while vLLM is running"
      fi
    }
  fi
done

[[ $SHM_ROOT != / ]] || fail 'refusing to use filesystem root as shared memory root'
install -d -m 1777 "$SHM_ROOT"
rm -rf -- "$SHM_ROOT/vllm-pair-scheduler"

create_container() {
  local container=$1
  "$CTR_BIN" -n "$NAMESPACE" run \
    --detach \
    --net-host \
    --env PYTORCH_NPU_ALLOC_CONF=max_split_size_mb:256 \
    "${device_args[@]}" \
    --device /dev/davinci_manager \
    --device /dev/devmm_svm \
    --device /dev/hisi_hdc \
    --mount type=bind,src=/usr/local/Ascend/driver,dst=/usr/local/Ascend/driver,options=rbind:ro \
    --mount type=bind,src=/usr/local/dcmi,dst=/usr/local/dcmi,options=rbind:ro \
    --mount type=bind,src=/usr/local/sbin/npu-smi,dst=/usr/local/sbin/npu-smi,options=bind:ro \
    --mount type=bind,src=/etc/ascend_install.info,dst=/etc/ascend_install.info,options=bind:ro \
    --mount "type=bind,src=$MODEL_ROOT,dst=/opt/model,options=rbind:ro" \
    --mount "type=bind,src=$SHM_ROOT,dst=/dev/shm,options=rbind:rw" \
    --mount "type=bind,src=$ROOT,dst=$ROOT,options=rbind:rw" \
    --mount "type=bind,src=$DEPS_ROOT,dst=$DEPS_ROOT,options=rbind:ro" \
    --mount "type=bind,src=$TOOLS_ROOT,dst=$TOOLS_ROOT,options=rbind:ro" \
    "$IMAGE" "$container" /bin/bash -lc \
    'trap : TERM INT; sleep infinity & wait'
}

echo '=== REPLACE EXACT NATIVE EXPERIMENT CONTAINERS ==='
delete_exact_container "$PRIMARY_CONTAINER"
delete_exact_container "$STANDBY_CONTAINER"

if ! create_container "$PRIMARY_CONTAINER"; then
  delete_exact_container "$PRIMARY_CONTAINER"
  fail "failed to create $PRIMARY_CONTAINER"
fi
if ! create_container "$STANDBY_CONTAINER"; then
  delete_exact_container "$STANDBY_CONTAINER"
  delete_exact_container "$PRIMARY_CONTAINER"
  fail "failed to create $STANDBY_CONTAINER"
fi

for container in "$PRIMARY_CONTAINER" "$STANDBY_CONTAINER"; do
  status=$(task_field "$container" 3)
  [[ $status == RUNNING ]] || fail "container task is not running: $container"
  container_exec "$container" /bin/bash -lc '
      set -euo pipefail
      wheel=$1
      expected_version=$2
      tools_root=$3
      ln -sfn "$tools_root/git/bin/git" /usr/local/bin/git
      test -d /vllm-workspace/vllm/vllm
      command -v gcc
      command -v git
      command -v python
      python -m pip install --force-reinstall --no-deps "$wheel"
      python -c "import importlib.metadata as m; value=m.version(\"xlite\"); print(\"XLITE_VERSION=\" + value); assert value == \"$expected_version\""
      npu-smi info >/dev/null
    ' _ "$XLITE_WHEEL" "$XLITE_EXPECTED_VERSION" "$TOOLS_ROOT"
done

echo "NATIVE_XLITE_CONTAINERS_READY containers=$PRIMARY_CONTAINER,$STANDBY_CONTAINER image=$IMAGE physical_npus=$PHYSICAL_NPUS"
