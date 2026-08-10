#!/usr/bin/env bash
set -euo pipefail

NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
CONTAINER=${XCCL_PROBE_CONTAINER:-cont1_ljw}
CTR_BIN=${CTR_BIN:-/root/l00933108/.tools/containerd/bin/ctr}
REPO=${XCCL_PROBE_REPO:-/root/l00933108/vllm-stack-yellow-zone}
CONFIG=${XCCL_PROBE_CONFIG:-configs/xccl-allreduce-x/cann-8.5.1-ascend910b4.yaml}

pid=$(
  "$CTR_BIN" -n "$NAMESPACE" tasks list |
    awk -v container="$CONTAINER" 'NR > 1 && $1 == container {print $2; found=1} END {exit !found}'
)
[[ $pid =~ ^[0-9]+$ ]]

declare -a container_env=()
while IFS= read -r -d '' item; do
  container_env+=("$item")
done <"/proc/$pid/environ"

exec nsenter --target "$pid" --mount --uts --ipc --net --pid \
  --root="/proc/$pid/root" --wd="/proc/$pid/root" -- \
  /usr/bin/env -i "${container_env[@]}" /bin/bash -lc '
    set -euo pipefail
    repo=$1
    config=$2
    shift 2
    if [[ $config != /* ]]; then
      config=$repo/$config
    fi
    workdir=/tmp/xccl-allreduce-x-work
    mkdir -p "$workdir"
    cd "$workdir"
    exec python "$repo/scripts/run-xccl-allreduce-x-matrix.py" \
      --config "$config" "$@"
  ' _ "$REPO" "$CONFIG" "$@"
