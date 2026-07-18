#!/usr/bin/env bash

set -Eeuo pipefail

IMAGE="${IMAGE:-quay.io/ascend/vllm-ascend:v0.19.1rc1}"
REPO_ROOT="${REPO_ROOT:-/root/l00933108}"
CONTAINER_NAME="${CONTAINER_NAME:-tp8-hccl-deadlock-repro}"
TENSOR_MIB="${TENSOR_MIB:-4}"
HANG_TIMEOUT="${HANG_TIMEOUT:-30}"
STARTUP_TIMEOUT="${STARTUP_TIMEOUT:-300}"
SUBMIT_TIMEOUT="${SUBMIT_TIMEOUT:-60}"
HCCL_TIMEOUT="${HCCL_TIMEOUT:-1800}"
STAGGER_SECONDS="${STAGGER_SECONDS:-2}"
LOG_FILE="${LOG_FILE:-/tmp/tp8-hccl-deadlock-$(date +%Y%m%d-%H%M%S).log}"

readonly IMAGE REPO_ROOT CONTAINER_NAME TENSOR_MIB HANG_TIMEOUT
readonly STARTUP_TIMEOUT SUBMIT_TIMEOUT HCCL_TIMEOUT STAGGER_SECONDS LOG_FILE

REPRO_SCRIPT="$REPO_ROOT/scripts/repro-tp8-hccl-deadlock.py"
readonly REPRO_SCRIPT

log() {
  printf '\n===== %s =====\n' "$*"
}

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  printf 'RESULT=LAUNCHER_PRECHECK_FAILED exit_code=2\n' >&2
  exit 2
}

command -v docker >/dev/null 2>&1 || fail "docker is not installed"

for path in \
  "$REPRO_SCRIPT" \
  /dev/davinci0 \
  /dev/davinci1 \
  /dev/davinci2 \
  /dev/davinci3 \
  /dev/davinci4 \
  /dev/davinci5 \
  /dev/davinci6 \
  /dev/davinci7 \
  /dev/davinci_manager \
  /dev/devmm_svm \
  /dev/hisi_hdc \
  /usr/local/dcmi \
  /usr/local/bin/npu-smi \
  /usr/local/Ascend/driver/lib64 \
  /usr/local/Ascend/driver/version.info \
  /etc/ascend_install.info
do
  [[ -e "$path" ]] || fail "required path not found: $path"
done

log "Configuration"
printf 'image=%s\ncontainer=%s\nscript=%s\nlog=%s\n' \
  "$IMAGE" "$CONTAINER_NAME" "$REPRO_SCRIPT" "$LOG_FILE"
printf 'tensor_mib=%s\nstagger_seconds=%s\nhang_timeout=%s\n' \
  "$TENSOR_MIB" "$STAGGER_SECONDS" "$HANG_TIMEOUT"

# Replace only the fixed reproducer container. No unrelated container is
# inspected or modified.
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

log "Run torch-npu precheck, then the crossed TP8 reproducer"
set +e
docker run --rm \
  --name "$CONTAINER_NAME" \
  --user 0:0 \
  --security-opt label=disable \
  --shm-size=16g \
  --device /dev/davinci0:/dev/davinci0 \
  --device /dev/davinci1:/dev/davinci1 \
  --device /dev/davinci2:/dev/davinci2 \
  --device /dev/davinci3:/dev/davinci3 \
  --device /dev/davinci4:/dev/davinci4 \
  --device /dev/davinci5:/dev/davinci5 \
  --device /dev/davinci6:/dev/davinci6 \
  --device /dev/davinci7:/dev/davinci7 \
  --device /dev/davinci_manager:/dev/davinci_manager \
  --device /dev/devmm_svm:/dev/devmm_svm \
  --device /dev/hisi_hdc:/dev/hisi_hdc \
  --volume /usr/local/dcmi:/usr/local/dcmi:ro \
  --volume /usr/local/bin/npu-smi:/usr/local/bin/npu-smi:ro \
  --volume /usr/local/Ascend/driver/lib64:/usr/local/Ascend/driver/lib64:ro \
  --volume /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info:ro \
  --volume /etc/ascend_install.info:/etc/ascend_install.info:ro \
  --volume "$REPRO_SCRIPT:/workspace/repro-tp8-hccl-deadlock.py:ro" \
  --env ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 \
  --env ASCEND_SLOG_PRINT_TO_STDOUT=1 \
  --env ASCEND_GLOBAL_LOG_LEVEL=1 \
  --env HCCL_EXEC_TIMEOUT="$HCCL_TIMEOUT" \
  --env TENSOR_MIB="$TENSOR_MIB" \
  --env STAGGER_SECONDS="$STAGGER_SECONDS" \
  --env HANG_TIMEOUT="$HANG_TIMEOUT" \
  --env STARTUP_TIMEOUT="$STARTUP_TIMEOUT" \
  --env SUBMIT_TIMEOUT="$SUBMIT_TIMEOUT" \
  --env HCCL_TIMEOUT="$HCCL_TIMEOUT" \
  "$IMAGE" \
  bash -lc '
set -o pipefail
printf "===== CONTAINER RUNTIME =====\n"
python3 -c '\''import platform, torch, torch_npu; print("python=" + platform.python_version()); print("torch=" + torch.__version__); print("torch_npu=" + torch_npu.__version__); print("visible_npus=" + str(torch.npu.device_count()))'\''
printf "\n===== SINGLE-NPU ACL/TENSOR PRECHECK =====\n"
python3 -c '\''import torch, torch_npu; torch.npu.set_device(0); value=torch.ones(1, device="npu:0"); torch.npu.synchronize(); print("tensor=" + str(value.cpu()))'\''
precheck_status=$?
if [ "$precheck_status" -ne 0 ]; then
  printf "RESULT=PRECHECK_FAILED exit_code=2 original_status=%d\n" "$precheck_status" >&2
  exit 2
fi
printf "RESULT=PRECHECK_PASSED\n"
printf "\n===== CROSSED TP8 HCCL REPRODUCER =====\n"
exec python3 /workspace/repro-tp8-hccl-deadlock.py \
  --tensor-mib "$TENSOR_MIB" \
  --stagger-seconds "$STAGGER_SECONDS" \
  --hang-timeout "$HANG_TIMEOUT" \
  --startup-timeout "$STARTUP_TIMEOUT" \
  --submit-timeout "$SUBMIT_TIMEOUT" \
  --hccl-timeout "$HCCL_TIMEOUT"
' 2>&1 | tee "$LOG_FILE"
container_status="${PIPESTATUS[0]}"
set -e

log "Final result"
printf 'container_exit_code=%s\nlog_file=%s\n' "$container_status" "$LOG_FILE"
grep 'RESULT=' "$LOG_FILE" || true
exit "$container_status"
