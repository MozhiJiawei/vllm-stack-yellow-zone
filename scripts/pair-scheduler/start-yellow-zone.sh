#!/usr/bin/env bash
set -euo pipefail

NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
PRIMARY_CONTAINER=${PRIMARY_CONTAINER:-cont1_ljw}
STANDBY_CONTAINER=${STANDBY_CONTAINER:-cont2_ljw}
STARTUP_TIMEOUT_SECONDS=${STARTUP_TIMEOUT_SECONDS:-900}
STARTUP_POLL_SECONDS=${STARTUP_POLL_SECONDS:-2}
CTR_BIN=${CTR_BIN:-ctr}

start_instance() {
  local container=$1
  local instance=$2
  local port=$3
  local master_port=$4
  local socket_range=$5
  local log=$6

  "$CTR_BIN" -n "$NAMESPACE" tasks exec \
    --exec-id "pair-start-$instance-$RANDOM" "$container" \
    /bin/bash -lc "
      set -euo pipefail
      cd /workspace
      unset ASCEND_RT_VISIBLE_DEVICES
      export ENPU_LOG_LEVEL=4
      export MASTER_PORT='$master_port'
      export HCCL_SOCKET_PORT_RANGE='$socket_range'
      nohup vllm serve /opt/model/Qwen3-4B/ \
        --max_model_len 10240 \
        --tensor-parallel-size 4 \
        --max-num-batched-tokens 1024 \
        --gpu-memory-utilization 0.85 \
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
    if "$CTR_BIN" -n "$NAMESPACE" tasks exec \
      --exec-id "pair-ready-$instance-$RANDOM" "$container" \
      /bin/bash -lc "
        set -euo pipefail
        curl --fail --silent --max-time 2 http://127.0.0.1:$port/v1/models >/dev/null
        vllm-pair-scheduler-inspect --json |
          python -c 'import json,sys; value=json.load(sys.stdin); assert value[\"state\"] == \"RUNNING\"; assert value[\"instances\"][\"$instance\"][\"registration_complete\"]'
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

start_instance "$STANDBY_CONTAINER" B 10041 29510 \
  62000-62050 /workspace/llm-4b-pair-cont2.log
wait_ready "$STANDBY_CONTAINER" B 10041

echo "PAIR_READY pair_id=default ports=10040,10041"
echo "Inspect with: vllm-pair-scheduler-inspect --json"
