#!/usr/bin/env bash
set -Eeuo pipefail

CTR_BIN=${CTR_BIN:-/root/l00933108/.tools/containerd/bin/ctr}
NAMESPACE=${CONTAINERD_NAMESPACE:-k8s.io}
IMAGE=${AISBENCH_IMAGE:-docker.io/library/mindie:2.3.1.B020-800I-A2-py3.11-openeuler24.03-lts-aarch64}
MODEL_ROOT=${MODEL_ROOT:-/cache/models}
ARTIFACT_ROOT=${ARTIFACT_ROOT:-/root/l00933108/.artifacts/aisbench}
INPUT_TOKENS=${INPUT_TOKENS:-256}
OUTPUT_TOKENS=${OUTPUT_TOKENS:-128}
REQUESTS_PER_INSTANCE=${REQUESTS_PER_INSTANCE:-32}
BATCH_SIZE=${BATCH_SIZE:-4}
REQUEST_RATE=${REQUEST_RATE:-0}
DRY_RUN=${DRY_RUN:-0}
TARGETS=${TARGETS:-AB}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

[[ -x $CTR_BIN ]] || fail "ctr client is missing: $CTR_BIN"
[[ -d $MODEL_ROOT/Qwen3-4B ]] || fail "model is missing: $MODEL_ROOT/Qwen3-4B"
for value in "$INPUT_TOKENS" "$OUTPUT_TOKENS" \
  "$REQUESTS_PER_INSTANCE" "$BATCH_SIZE"; do
  [[ $value =~ ^[1-9][0-9]*$ ]] || fail "positive integer required: $value"
done
[[ $DRY_RUN == 0 || $DRY_RUN == 1 ]] || fail "DRY_RUN must be 0 or 1"
[[ $TARGETS == A || $TARGETS == AB ]] || fail "TARGETS must be A or AB"

ports=(10040)
[[ $TARGETS == A ]] || ports+=(10041)
for port in "${ports[@]}"; do
  curl --fail --silent --max-time 3 \
    "http://127.0.0.1:$port/v1/models" >/dev/null ||
    fail "vLLM service is not ready on port $port"
done

run_id=$(date -u +%Y%m%dT%H%M%SZ)
run_root="$ARTIFACT_ROOT/$run_id"
install -d -m 0755 "$run_root/A" "$run_root/B"

run_client() {
  local label=$1
  local port=$2
  local output_dir=$3
  local name="aisbench_${label,,}_${run_id,,}"
  local dry_arg=()
  [[ $DRY_RUN == 0 ]] || dry_arg=(--dry-run)

  "$CTR_BIN" -n "$NAMESPACE" tasks delete --force "$name" \
    >/dev/null 2>&1 || true
  "$CTR_BIN" -n "$NAMESPACE" containers delete "$name" \
    >/dev/null 2>&1 || true

  "$CTR_BIN" -n "$NAMESPACE" run --rm --net-host \
    --env "AISBENCH_PORT=$port" \
    --env "AISBENCH_INPUT_TOKENS=$INPUT_TOKENS" \
    --env "AISBENCH_OUTPUT_TOKENS=$OUTPUT_TOKENS" \
    --env "AISBENCH_REQUESTS=$REQUESTS_PER_INSTANCE" \
    --env "AISBENCH_BATCH_SIZE=$BATCH_SIZE" \
    --env "AISBENCH_REQUEST_RATE=$REQUEST_RATE" \
    --mount "type=bind,src=/usr/local/Ascend/driver,dst=/usr/local/Ascend/driver,options=rbind:ro" \
    --mount "type=bind,src=$MODEL_ROOT,dst=/opt/model,options=rbind:ro" \
    --mount "type=bind,src=$output_dir,dst=/results,options=rbind:rw" \
    "$IMAGE" "$name" /bin/bash -lc '
      set -euo pipefail
      package_root=/usr/local/lib/python3.11/site-packages/ais_bench
      export AISBENCH_MODEL_CONFIG="$package_root/benchmark/configs/models/vllm_api/vllm_api_general_stream.py"
      export AISBENCH_SYNTHETIC_CONFIG="$package_root/datasets/synthetic/synthetic_config.py"
      python3 - <<"PY"
import os
from pathlib import Path


def replace_once(text: str, old: str, new: str, path: Path) -> str:
    if text.count(old) != 1:
        raise SystemExit(f"expected one {old!r} in {path}")
    return text.replace(old, new)


model_path = Path(os.environ["AISBENCH_MODEL_CONFIG"])
port = os.environ["AISBENCH_PORT"]
output_tokens = os.environ["AISBENCH_OUTPUT_TOKENS"]
batch_size = os.environ["AISBENCH_BATCH_SIZE"]
request_rate = os.environ["AISBENCH_REQUEST_RATE"]
text = model_path.read_text(encoding="utf-8")
replacements = {
    "path=\"\"": "path=\"/opt/model/Qwen3-4B\"",
    "model=\"\"": "model=\"Qwen3-4B\"",
    "host_port = 8080": f"host_port = {port}",
    "max_out_len = 512": f"max_out_len = {output_tokens}",
    "batch_size=1": f"batch_size={batch_size}",
    "request_rate = 0": f"request_rate = {request_rate}",
    "generation_kwargs = dict(": "generation_kwargs = dict(ignore_eos=True,",
}
for old, new in replacements.items():
    text = replace_once(text, old, new, model_path)
model_path.write_text(text, encoding="utf-8")

synthetic_path = Path(os.environ["AISBENCH_SYNTHETIC_CONFIG"])
requests = os.environ["AISBENCH_REQUESTS"]
input_tokens = os.environ["AISBENCH_INPUT_TOKENS"]
text = synthetic_path.read_text(encoding="utf-8")
text = replace_once(
    text,
    "\"RequestCount\": 10",
    f"\"RequestCount\": {requests}",
    synthetic_path,
)
text = replace_once(
    text,
    "\"RequestSize\": 10",
    f"\"RequestSize\": {input_tokens}",
    synthetic_path,
)
synthetic_path.write_text(text, encoding="utf-8")
PY
      cd /usr/local/lib/python3.11/site-packages
      exec ais_bench \
        --models vllm_api_general_stream \
        --datasets synthetic_gen \
        --mode perf \
        --num-prompts "$AISBENCH_REQUESTS" \
        --debug \
        --work-dir /results/output \
        "$@"
    ' _ "${dry_arg[@]}" >"$output_dir/client.log" 2>&1
}

echo "AISBENCH_START run=$run_id targets=$TARGETS input=$INPUT_TOKENS output=$OUTPUT_TOKENS requests_per_instance=$REQUESTS_PER_INSTANCE batch=$BATCH_SIZE request_rate=$REQUEST_RATE dry_run=$DRY_RUN"
run_client A 10040 "$run_root/A" &
pid_a=$!
pid_b=
if [[ $TARGETS == AB ]]; then
  run_client B 10041 "$run_root/B" &
  pid_b=$!
fi

status=0
wait "$pid_a" || status=1
if [[ -n $pid_b ]]; then
  wait "$pid_b" || status=1
fi
if (( status != 0 )); then
  echo "ERROR: AISBench failed; inspect $run_root/A/client.log and $run_root/B/client.log" >&2
  exit "$status"
fi

echo "AISBENCH_COMPLETE run=$run_id results=$run_root"
find "$run_root" -type f -printf '%p\n' | sort
