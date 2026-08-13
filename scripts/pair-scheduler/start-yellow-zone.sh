#!/usr/bin/env bash
set -euo pipefail

NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
PRIMARY_CONTAINER=${PRIMARY_CONTAINER:-cont1_ljw}
STANDBY_CONTAINER=${STANDBY_CONTAINER:-cont2_ljw}
STARTUP_TIMEOUT_SECONDS=${STARTUP_TIMEOUT_SECONDS:-900}
STARTUP_POLL_SECONDS=${STARTUP_POLL_SECONDS:-2}
GPU_MEMORY_UTILIZATION=${GPU_MEMORY_UTILIZATION:-0.35}
CTR_BIN=${CTR_BIN:-/root/l00933108/.tools/containerd/bin/ctr}
TARGETS=${TARGETS:-AB}
REQUIRE_SCHEDULER=${REQUIRE_SCHEDULER:-1}
PAIR_TRACE_ROOT=${PAIR_TRACE_ROOT:-}
PAIR_DIAGNOSTICS_INTERVAL=${PAIR_DIAGNOSTICS_INTERVAL:-}

[[ $TARGETS == A || $TARGETS == AB ]] || {
  echo "ERROR: TARGETS must be A or AB" >&2
  exit 1
}
[[ $REQUIRE_SCHEDULER == 0 || $REQUIRE_SCHEDULER == 1 ]] || {
  echo "ERROR: REQUIRE_SCHEDULER must be 0 or 1" >&2
  exit 1
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
  pid=$(task_field "$container" 2)
  [[ $pid =~ ^[0-9]+$ ]]
  while IFS= read -r -d '' item; do
    container_env+=("$item")
  done <"/proc/$pid/environ"
  nsenter --target "$pid" --mount --uts --ipc --net --pid \
    --root="/proc/$pid/root" --wd="/proc/$pid/root" -- \
    /usr/bin/env -i "${container_env[@]}" "$@"
}

start_instance() {
  local container=$1
  local instance=$2
  local port=$3
  local master_port=$4
  local socket_range=$5
  local log=$6

  container_exec "$container" /bin/bash -lc "
      set -euo pipefail
      cd /workspace
      unset ASCEND_RT_VISIBLE_DEVICES
      export ENPU_LOG_LEVEL=4
      export MASTER_PORT='$master_port'
      export HCCL_HOST_SOCKET_PORT_RANGE='$socket_range'
      export HCCL_NPU_SOCKET_PORT_RANGE='$socket_range'
      if [[ -n '$PAIR_TRACE_ROOT' ]]; then
        export VLLM_PAIR_SCHED_TRACE_DIR='$PAIR_TRACE_ROOT/$instance'
        install -d -m 0755 \"\$VLLM_PAIR_SCHED_TRACE_DIR\"
      fi
      if [[ -n '$PAIR_DIAGNOSTICS_INTERVAL' ]]; then
        export VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL='$PAIR_DIAGNOSTICS_INTERVAL'
      fi
      nohup vllm serve /opt/model/Qwen3-4B/ \
        --max_model_len 10240 \
        --tensor-parallel-size 4 \
        --max-num-batched-tokens 1024 \
        --gpu-memory-utilization '$GPU_MEMORY_UTILIZATION' \
        --async-scheduling \
        --block-size 128 \
        --additional-config='{\"xlite_graph_config\":{\"enabled\":true,\"full_mode\":true}}' \
        --host 0.0.0.0 \
        --port '$port' \
        --served-model-name Qwen3-4B \
        > '$log' 2>&1 < /dev/null &
      echo \$! > '$log.pid'
      echo started pid=\$! log='$log'
    "
}

wait_ready() {
  local container=$1
  local instance=$2
  local port=$3
  local deadline=$((SECONDS + STARTUP_TIMEOUT_SECONDS))

  while (( SECONDS < deadline )); do
    if container_exec "$container" /bin/bash -lc "
        set -euo pipefail
        curl --fail --silent --max-time 2 http://127.0.0.1:$port/v1/models >/dev/null
        if [[ $REQUIRE_SCHEDULER == 1 ]]; then
          vllm-pair-scheduler-inspect --json |
            python -c 'import json,sys; value=json.load(sys.stdin); assert value[\"state\"] == \"RUNNING\"; assert value[\"instances\"][\"$instance\"][\"registration_complete\"]'
        fi
      " >/dev/null 2>&1; then
      echo "PAIR_INSTANCE_READY instance=$instance container=$container port=$port"
      return 0
    fi
    sleep "$STARTUP_POLL_SECONDS"
  done

  echo "PAIR_INSTANCE_TIMEOUT instance=$instance container=$container timeout=${STARTUP_TIMEOUT_SECONDS}s" >&2
  return 1
}

start_instance "$PRIMARY_CONTAINER" A 10040 29504 \
  61000-61050 /workspace/llm-4b-pair-cont1.log
wait_ready "$PRIMARY_CONTAINER" A 10040

if [[ $TARGETS == AB ]]; then
  start_instance "$STANDBY_CONTAINER" B 10041 29510 \
    62000-62050 /workspace/llm-4b-pair-cont2.log
  wait_ready "$STANDBY_CONTAINER" B 10041
fi

echo "MODEL_READY targets=$TARGETS scheduler_required=$REQUIRE_SCHEDULER"
if [[ $REQUIRE_SCHEDULER == 1 ]]; then
  echo "Inspect with: vllm-pair-scheduler-inspect --json"
fi
