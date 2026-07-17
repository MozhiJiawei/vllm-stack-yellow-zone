#!/usr/bin/env bash

set -Eeuo pipefail

IMAGE="${IMAGE:-quay.io/ascend/vllm-ascend:v0.19.1rc1}"
DEVICE="${DEVICE:-/dev/davinci0}"
CONTAINER_NAME="${CONTAINER_NAME:-vllm-ascend-native-diagnose}"

readonly IMAGE DEVICE CONTAINER_NAME

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

for path in \
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
  [[ -e "$path" ]] || fail "required path not found: $path"
done

printf '===== HOST DRIVER =====\n'
cat /usr/local/Ascend/driver/version.info
printf '\n===== HOST INSTALL INFO =====\n'
cat /etc/ascend_install.info
printf '\n===== HOST DEVICE NODES =====\n'
ls -l "$DEVICE" /dev/davinci_manager /dev/devmm_svm /dev/hisi_hdc

docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

printf '\n===== CONTAINER ACL DIAGNOSIS =====\n'
docker run --rm \
  --name "$CONTAINER_NAME" \
  --user 0:0 \
  --security-opt label=disable \
  --device "$DEVICE:$DEVICE" \
  --device /dev/davinci_manager:/dev/davinci_manager \
  --device /dev/devmm_svm:/dev/devmm_svm \
  --device /dev/hisi_hdc:/dev/hisi_hdc \
  --volume /usr/local/dcmi:/usr/local/dcmi:ro \
  --volume /usr/local/bin/npu-smi:/usr/local/bin/npu-smi:ro \
  --volume /usr/local/Ascend/driver/lib64:/usr/local/Ascend/driver/lib64:ro \
  --volume /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info:ro \
  --volume /etc/ascend_install.info:/etc/ascend_install.info:ro \
  --entrypoint bash \
  "$IMAGE" -lc '
set +e

printf "===== CONTAINER VERSIONS =====\n"
python -c "import platform, torch, torch_npu; print(\"python=\" + platform.python_version()); print(\"torch=\" + torch.__version__); print(\"torch_npu=\" + torch_npu.__version__)"
version_status=$?
cat /usr/local/Ascend/driver/version.info

printf "\n===== CONTAINER ASCEND ENV =====\n"
env | grep -E "^((ASCEND|ATB|PYTORCH_NPU|SOC_VERSION|TASK_QUEUE)[A-Z0-9_]*|LD_LIBRARY_PATH)=" | sort

printf "\n===== CONTAINER DRIVER LIBRARIES =====\n"
ls -ld /usr/local/Ascend/driver/lib64 /usr/local/Ascend/driver/lib64/common /usr/local/Ascend/driver/lib64/driver
ls -l /usr/local/Ascend/driver/lib64/libascend_hal.so* /usr/local/Ascend/driver/lib64/driver/libascend_hal.so* 2>&1
ldconfig -p | grep -E "lib(ascendcl|ascend_hal|runtime|ge_runner|graph)\.so" || true

printf "\n===== NPU-SMI IN CONTAINER =====\n"
npu-smi info
npu_smi_status=$?
printf "npu_smi_status=%d\n" "$npu_smi_status"

printf "\n===== TORCH-NPU SOC INITIALIZATION =====\n"
ASCEND_GLOBAL_LOG_LEVEL=1 python -c "import torch_npu; print(\"soc_version=\" + str(torch_npu.npu.get_soc_version()))"
soc_status=$?
printf "soc_init_status=%d\n" "$soc_status"

printf "\n===== TORCH-NPU TENSOR ALLOCATION =====\n"
if [ "$soc_status" -eq 0 ]; then
  ASCEND_GLOBAL_LOG_LEVEL=1 python -c "import torch, torch_npu; value=torch.ones(1).npu(); print(value.cpu())"
  tensor_status=$?
else
  printf "SKIPPED: SoC initialization failed\n"
  tensor_status=125
fi
printf "tensor_status=%d\n" "$tensor_status"

printf "\nRESULT: version=%d npu_smi=%d soc_init=%d tensor=%d\n" \
  "$version_status" "$npu_smi_status" "$soc_status" "$tensor_status"
exit 0
'
