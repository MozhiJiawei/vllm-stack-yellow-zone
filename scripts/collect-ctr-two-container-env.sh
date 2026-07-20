#!/usr/bin/env bash
# Collect the minimum host-side facts needed to translate the known Docker
# launch into two ctr-managed vCANN containers. No containers need to exist.
# This script is read-only and does not inspect NPU state, model contents,
# package inventories, credentials, or unrelated containers/processes.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: collect-ctr-two-container-env.sh [OPTIONS]

Options:
  --namespace NAME  containerd namespace (default: k8s.io)
  --image REF       target image reference
  -h, --help        show this help
EOF
}

namespace=k8s.io
image=quay.io/ascend/vllm-ascend:v0.19.1rc1

while (($#)); do
  case "$1" in
    --namespace)
      namespace=${2:-}
      shift 2
      ;;
    --image)
      image=${2:-}
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      printf 'ERROR: unknown argument: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "$namespace" || -z "$image" ]]; then
  printf 'ERROR: namespace and image must be non-empty\n' >&2
  exit 2
fi

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'ERROR: required command not found: %s\n' "$1" >&2
    exit 2
  fi
}

path_status() {
  local path=$1
  if [[ -e "$path" ]]; then
    if [[ -r "$path" ]]; then
      stat -Lc 'path=%n type=%F mode=%a readable=yes' "$path"
    else
      stat -Lc 'path=%n type=%F mode=%a readable=no' "$path"
    fi
  else
    printf 'path=%s status=missing\n' "$path"
  fi
}

require_command ctr
require_command awk
require_command find
require_command grep
require_command python3
require_command sed
require_command sort
require_command stat

printf '=== requested topology ===\n'
printf 'namespace=%s\nimage=%s\ncontainer_a=xlite-xccl-a\ncontainer_b=xlite-xccl-b\n' \
  "$namespace" "$image"

printf '=== ctr/containerd ===\n'
ctr version | sed -n '1,8p'
printf 'namespace_present='
if ctr namespaces list 2>/dev/null | awk -v ns="$namespace" 'NR > 1 && $1 == ns {found=1} END {exit !found}'; then
  printf 'yes\n'
else
  printf 'no\n'
fi

printf 'target_image='
if ctr -n "$namespace" images list 2>/dev/null | awk -v image="$image" 'NR > 1 && $1 == image {found=1} END {exit !found}'; then
  printf 'present\n'
else
  printf 'missing\n'
fi

printf 'container_id_xlite-xccl-a='
if ctr -n "$namespace" containers info xlite-xccl-a >/dev/null 2>&1; then
  printf 'occupied\n'
else
  printf 'available\n'
fi
printf 'container_id_xlite-xccl-b='
if ctr -n "$namespace" containers info xlite-xccl-b >/dev/null 2>&1; then
  printf 'occupied\n'
else
  printf 'available\n'
fi

printf '=== required ctr run flags ===\n'
run_help=$(ctr run --help 2>&1)
for flag in detach device env mount net-host privileged tty; do
  if grep -Eq -- "--${flag}([=,[:space:]]|$)" <<<"$run_help"; then
    printf '%s=yes\n' "$flag"
  else
    printf '%s=no\n' "$flag"
  fi
done

printf '=== host coordinator ===\n'
printf 'python3=%s\n' "$(command -v python3)"
python3 --version
python3 -c '
import socket

for port in (23000, 23400, 24000, 24400, 29680, 29681, 29504, 29510):
    sock = socket.socket()
    try:
        sock.bind(("127.0.0.1", port))
    except OSError as error:
        print(f"port_{port}=busy:{error.errno}")
    else:
        print(f"port_{port}=available")
    finally:
        sock.close()
'

printf '=== exact device sources from old Docker launch ===\n'
for path in \
  /dev/davinci0 /dev/davinci1 /dev/davinci2 /dev/davinci3 \
  /dev/davinci4 /dev/davinci5 /dev/davinci6 /dev/davinci7 \
  /dev/davinci_manager /dev/devmm_svm /dev/hisi_hdc; do
  path_status "$path"
done

printf '=== exact bind sources from old Docker launch ===\n'
for path in \
  /cache/isa/Qwen3-4B \
  /cache/isa/Qwen3-32B \
  /opt/isa/shm \
  /usr/local/Ascend/driver \
  /usr/local/Ascend/driver/lib64/common \
  /usr/local/Ascend/driver/lib64/driver \
  /usr/local/Ascend/firmware \
  /usr/local/sbin/npu-smi \
  /usr/local/sbin \
  /usr/local/dcmi \
  /root/isa/bins/libvruntime.so \
  /root/isa/bins/enpu-monitor \
  /root/isa/conf/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl \
  /root/l00933108; do
  path_status "$path"
done

printf '=== targeted preload candidates ===\n'
for path in \
  /root/l00933108/runtime/vcann-rt/preload/ld.so.preload \
  /root/isa/build/ld.so.preload \
  /root/isa/bins/ld.so.preload; do
  path_status "$path"
done

printf '=== vCANN config candidates and scheduling fields ===\n'
config_count=0
for directory in /root/isa/bins /root/l00933108/runtime/vcann-rt/config; do
  [[ -d "$directory" ]] || continue
  while IFS= read -r config; do
    config_count=$((config_count + 1))
    printf '%s\n' "--- config=$config"
    grep -E '^[[:space:]]*(\[DEVICE-[0-7]\]|virtual-npu-id|aicore-quota|memory-quota|shm-id|scheduling-policy)[[:space:]]*([=]|$)' \
      "$config" || true
  done < <(find "$directory" -maxdepth 1 -type f -name '*npu_info.config' -print | sort)
done
printf 'config_count=%d\n' "$config_count"

printf '=== exact reserved socket ranges ===\n'
if command -v ss >/dev/null 2>&1; then
  listeners=$(ss -lntup 2>/dev/null | awk '
    NR == 1 {next}
    {
      endpoint=$5
      sub(/^.*:/, "", endpoint)
      if (endpoint ~ /^[0-9]+$/ &&
          (endpoint == 29504 || endpoint == 29510 ||
           (endpoint >= 61000 && endpoint <= 61050) ||
           (endpoint >= 62000 && endpoint <= 62050))) print
    }
  ')
  if [[ -n "$listeners" ]]; then
    printf '%s\n' "$listeners"
  else
    printf 'reserved_socket_listeners=none\n'
  fi
else
  printf 'reserved_socket_listeners=ss-command-missing\n'
fi

printf 'RESULT=COLLECTION_COMPLETE\n'
