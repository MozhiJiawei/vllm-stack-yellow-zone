#!/usr/bin/env bash

set -Eeuo pipefail

# This baseline intentionally excludes XLite and vCANN-RT.

IMAGE="${IMAGE:-quay.io/ascend/vllm-ascend:v0.19.1rc1}"
MODEL_DIR="${MODEL_DIR:-/softwarePlatform/c00879303/Qwen3-8B}"
CONTAINER_MODEL_DIR="${CONTAINER_MODEL_DIR:-/models/Qwen3-8B}"
CONTAINER_NAME="${CONTAINER_NAME:-vllm-qwen3-8b-native}"
DEVICE="${DEVICE:-/dev/davinci0}"
SERVED_MODEL_NAME="${SERVED_MODEL_NAME:-Qwen3-8B}"
PORT="${PORT:-8000}"
READY_ATTEMPTS="${READY_ATTEMPTS:-180}"
READY_INTERVAL_SECONDS="${READY_INTERVAL_SECONDS:-5}"

readonly IMAGE MODEL_DIR CONTAINER_MODEL_DIR CONTAINER_NAME DEVICE
readonly SERVED_MODEL_NAME PORT
readonly READY_ATTEMPTS READY_INTERVAL_SECONDS

log() {
  printf '\n===== %s =====\n' "$*"
}

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

require_path() {
  [[ -e "$1" ]] || fail "required path not found: $1"
}

show_container_logs() {
  docker logs --tail 300 "$CONTAINER_NAME" 2>&1 || true
}

require_command docker

for path in \
  "$MODEL_DIR" \
  "$DEVICE" \
  /dev/davinci_manager \
  /dev/devmm_svm \
  /dev/hisi_hdc \
  /usr/local/dcmi \
  /usr/local/bin/npu-smi \
  /usr/local/Ascend/driver/lib64 \
  /usr/local/Ascend/driver/version.info \
  /etc/ascend_install.info
do
  require_path "$path"
done

log "Configuration"
printf 'image=%s\nmodel_dir=%s\ncontainer=%s\ndevice=%s\nport=%s\n' \
  "$IMAGE" "$MODEL_DIR" "$CONTAINER_NAME" "$DEVICE" "$PORT"

log "Replace only the named experiment container"
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

log "Start native vLLM-Ascend baseline"
docker run -d \
  --name "$CONTAINER_NAME" \
  --user 0:0 \
  --security-opt label=disable \
  --publish "127.0.0.1:$PORT:$PORT" \
  --shm-size 1g \
  --device "$DEVICE:$DEVICE" \
  --device /dev/davinci_manager:/dev/davinci_manager \
  --device /dev/devmm_svm:/dev/devmm_svm \
  --device /dev/hisi_hdc:/dev/hisi_hdc \
  --volume /usr/local/dcmi:/usr/local/dcmi:ro \
  --volume /usr/local/bin/npu-smi:/usr/local/bin/npu-smi:ro \
  --volume /usr/local/Ascend/driver/lib64:/usr/local/Ascend/driver/lib64:ro \
  --volume /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info:ro \
  --volume /etc/ascend_install.info:/etc/ascend_install.info:ro \
  --volume "$MODEL_DIR:$CONTAINER_MODEL_DIR:ro" \
  --env PYTORCH_NPU_ALLOC_CONF=max_split_size_mb:256 \
  "$IMAGE" \
  vllm serve "$CONTAINER_MODEL_DIR" \
    --served-model-name "$SERVED_MODEL_NAME" \
    --host 0.0.0.0 \
    --port "$PORT" \
    --max-model-len 4096 \
    --max-num-batched-tokens 2048 \
    --gpu-memory-utilization 0.8 \
    --block-size 128

log "Wait for the OpenAI-compatible API"
ready=0
models_response=""
for ((attempt = 1; attempt <= READY_ATTEMPTS; attempt++)); do
  if models_response="$(docker exec "$CONTAINER_NAME" python -c \
    "import json, sys, urllib.request; model, port=sys.argv[1:]; body=urllib.request.urlopen(f'http://127.0.0.1:{port}/v1/models', timeout=2).read().decode(); data=json.loads(body); assert model in [item['id'] for item in data['data']]; print(body)" \
    "$SERVED_MODEL_NAME" "$PORT" \
    2>/dev/null)"
  then
    ready=1
    printf 'API ready after %d attempt(s)\n' "$attempt"
    break
  fi

  running="$(docker inspect --format '{{.State.Running}}' "$CONTAINER_NAME" 2>/dev/null || true)"
  if [[ "$running" != "true" ]]; then
    printf 'ERROR: container exited before the API became ready\n' >&2
    show_container_logs
    exit 1
  fi

  if ((attempt % 12 == 0)); then
    printf 'Still waiting: attempt %d/%d\n' "$attempt" "$READY_ATTEMPTS"
  fi
  sleep "$READY_INTERVAL_SECONDS"
done

if [[ "$ready" -ne 1 ]]; then
  printf 'ERROR: API did not become ready after %d seconds\n' \
    "$((READY_ATTEMPTS * READY_INTERVAL_SECONDS))" >&2
  show_container_logs
  exit 1
fi

log "Model list"
printf '%s\n' "$models_response"

log "Minimal completion"
if ! completion_response="$(docker exec "$CONTAINER_NAME" python -c \
  "import json, sys, urllib.request; model, port=sys.argv[1:]; payload=json.dumps({'model':model,'prompt':'The capital of China is','max_tokens':8,'temperature':0}).encode(); request=urllib.request.Request(f'http://127.0.0.1:{port}/v1/completions', data=payload, headers={'Content-Type':'application/json'}); body=urllib.request.urlopen(request, timeout=120).read().decode(); data=json.loads(body); assert data['model']==model and data['choices'] and data['choices'][0]['text']; print(body)" \
  "$SERVED_MODEL_NAME" "$PORT" \
  2>&1)"
then
  printf 'ERROR: minimal completion failed: %s\n' "$completion_response" >&2
  show_container_logs
  exit 1
fi
printf '%s\n' "$completion_response"

if [[ "$(docker inspect --format '{{.State.Running}}' "$CONTAINER_NAME")" != "true" ]]; then
  printf 'ERROR: container exited after the completion request\n' >&2
  show_container_logs
  exit 1
fi

log "Recent container logs"
docker logs --tail 120 "$CONTAINER_NAME"

printf '\nRESULT: native vLLM Qwen3-8B single-card service passed and remains running as %s\n' \
  "$CONTAINER_NAME"
