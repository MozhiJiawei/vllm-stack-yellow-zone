#!/usr/bin/env bash
# Rebuild and atomically replace the diagnostic vCANN runtime. The established
# containers use a directory bind mount, so newly started processes see the new
# library without a container restart. Pass --restart to recreate the pair.

set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: restart-vcann-xlite-containers.sh [OPTIONS]

By default, rebuild vCANN in the currently running build container and
atomically replace the runtime used by new processes. Existing processes keep
their already loaded runtime. Pass --restart to recreate cont1_ljw/cont2_ljw
with the proven ctr configuration and install xLite and GDB.

Options:
  --namespace NAME       containerd namespace (default: k8s.io)
  --image REF            existing local image (default: vllm:19)
  --repo-root PATH       host/repository path (default: /root/l00933108)
  --build-container ID   running container used to compile (default: cont1_ljw)
  --restart              recreate both containers after replacing the runtime
  --clean-build          remove only vcann-rt's build directory before building
  --skip-build           reuse the existing diagnostic runtime artifact
  --trace-enabled 0|1    ENPU_DEADLOCK_TRACE value (default: 1)
  -h, --help             show this help

Environment overrides:
  CONFIG_A, CONFIG_B, XLITE_WHEEL, XLITE_EXPECTED_VERSION,
  GDB_SOURCE
EOF
}

namespace=k8s.io
image=vllm:19
repo_root=/root/l00933108
build_container=cont1_ljw
clean_build=0
skip_build=0
restart_containers=0
trace_enabled=1

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
    --repo-root)
      repo_root=${2:-}
      shift 2
      ;;
    --build-container)
      build_container=${2:-}
      shift 2
      ;;
    --clean-build)
      clean_build=1
      shift
      ;;
    --restart)
      restart_containers=1
      shift
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --trace-enabled)
      trace_enabled=${2:-}
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

if [[ -z "$namespace" || -z "$image" || -z "$repo_root" ||
      -z "$build_container" ]]; then
  printf 'ERROR: namespace, image, repo-root and build-container must be non-empty\n' >&2
  exit 2
fi
if [[ "$trace_enabled" != 0 && "$trace_enabled" != 1 ]]; then
  printf 'ERROR: --trace-enabled must be 0 or 1\n' >&2
  exit 2
fi
if ((EUID != 0)); then
  printf 'ERROR: run this script as root on the containerd host\n' >&2
  exit 2
fi

config_a=${CONFIG_A:-$repo_root/cont1_npu_info.config}
config_b=${CONFIG_B:-$repo_root/cont2_npu_info.config}
xlite_wheel=${XLITE_WHEEL:-/root/isa/conf/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl}
gdb_source=${GDB_SOURCE:-/root/isa/gdb_arm}
runtime_dir=$repo_root/runtime/vcann-deadlock
runtime_artifact=$runtime_dir/libvruntime.so
compat_artifact=$repo_root/libvruntime-deadlock-diag.so
container_runtime_dir=/opt/enpu/vcann-rt/hot
generated_preload=$runtime_dir/ld.so.preload
vcann_src=$repo_root/vcann-rt/ubs-virt-enpu/vcann-rt
container_a=cont1_ljw
container_b=cont2_ljw
expected_xlite_version=${XLITE_EXPECTED_VERSION:-0.1.0rc12}

log() {
  printf '\n===== %s =====\n' "$*"
}

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

require_file() {
  [[ -s "$1" ]] || die "required file is missing or empty: $1"
}

unique_exec_id() {
  printf '%s-%s-%s' "$1" "$$" "$RANDOM"
}

task_field() {
  local container=$1
  local field=$2
  ctr -n "$namespace" tasks list | awk -v container="$container" -v field="$field" \
    'NR > 1 && $1 == container {print $field; found=1} END {exit !found}'
}

delete_exact_container() {
  local container=$1
  ctr -n "$namespace" tasks delete --force "$container" >/dev/null 2>&1 || true
  ctr -n "$namespace" containers delete "$container" >/dev/null 2>&1 || true
}

require_command ctr
require_command awk
require_command grep
require_command install
require_command ln
require_command mkdir
require_command sha256sum

[[ -d "$repo_root" ]] || die "repository directory not found: $repo_root"
[[ -d "$vcann_src" ]] || die "vCANN source directory not found: $vcann_src"
mkdir -p "$runtime_dir"

if ((restart_containers == 1)); then
  require_command python3
  require_file "$config_a"
  require_file "$config_b"
  require_file "$xlite_wheel"
  require_file "$gdb_source"
  require_file /root/isa/bins/enpu-monitor
  require_file /root/isa/bins/ld.so.preload
  [[ -d /usr/local/Ascend/driver ]] || die 'Ascend driver directory not found'
  [[ -d /cache/isa/Qwen3-4B ]] || die 'Qwen3-4B model directory not found'
  [[ -d /cache/isa/Qwen3-32B ]] || die 'Qwen3-32B model directory not found'
  [[ -d /opt/isa/shm ]] || die 'shared-memory directory not found: /opt/isa/shm'

  ctr -n "$namespace" images list -q | grep -Fx -- "$image" >/dev/null ||
    die "image is not present in namespace $namespace: $image"
fi

if ((skip_build == 0)); then
  build_status=$(task_field "$build_container" 3 2>/dev/null || true)
  [[ "$build_status" == RUNNING ]] ||
    die "build container task is not RUNNING: $build_container (use --skip-build only with a verified artifact)"

  log "build diagnostic vCANN in $build_container"
  ctr -n "$namespace" tasks exec \
    --exec-id "$(unique_exec_id rebuild-vcann)" \
    "$build_container" /bin/bash -lc '
      set -Eeuo pipefail
      repo_root=$1
      clean_build=$2
      runtime_artifact=$3
      vcann_src=$repo_root/vcann-rt/ubs-virt-enpu/vcann-rt

      cd "$vcann_src"
      test "$(realpath "$vcann_src")" = \
        "$(realpath "$repo_root")/vcann-rt/ubs-virt-enpu/vcann-rt"

      if [[ "$clean_build" == 1 && -d build ]]; then
        rm -rf -- "$vcann_src/build"
      elif [[ -f build/CMakeCache.txt ]]; then
        cached_source=$(sed -n "s/^CMAKE_HOME_DIRECTORY:INTERNAL=//p" build/CMakeCache.txt)
        if [[ -n "$cached_source" && "$cached_source" != "$vcann_src" ]]; then
          stale_build="build.stale.$(date +%Y%m%d-%H%M%S)"
          mv build "$stale_build"
          echo "OLD_BUILD_MOVED_TO=$vcann_src/$stale_build"
        fi
      fi

      if [[ -f /usr/local/Ascend/ascend-toolkit/set_env.sh ]]; then
        source /usr/local/Ascend/ascend-toolkit/set_env.sh
      fi
      : "${ASCEND_HOME_PATH:?ASCEND_HOME_PATH is not set}"

      grep -Eq "^#define VCANN_TRACE_ABI_VERSION +3U$" \
        src/include/deadlock_trace.h
      ENABLE_DEADLOCK_DIAGNOSTICS=1 bash make_build.sh
      test -s build/libvruntime.so

      artifact_tmp="$runtime_artifact.tmp.$$"
      cp build/libvruntime.so "$artifact_tmp"
      chmod 0755 "$artifact_tmp"
      mv -f "$artifact_tmp" "$runtime_artifact"

      for symbol in \
        aclrtBinaryGetFunction \
        g_vcann_trace \
        g_vcann_sync_probe \
        g_vcann_host_sync_probe \
        g_vcann_kernel_registry \
        vcann_trace_record_enabled; do
        readelf -sW "$runtime_artifact" | grep -F " $symbol" >/dev/null
      done
      ls -lh "$runtime_artifact"
      sha256sum "$runtime_artifact"
      echo VCANN_DIAGNOSTIC_BUILD_OK
    ' _ "$repo_root" "$clean_build" "$runtime_artifact"
else
  log 'reuse existing diagnostic vCANN artifact'
fi

require_file "$runtime_artifact"
runtime_sha256=$(sha256sum "$runtime_artifact" | awk '{print $1}')
ln -sfn runtime/vcann-deadlock/libvruntime.so "$compat_artifact"

if ((restart_containers == 0)); then
  log 'verify hot runtime visibility for new container processes'
  migrated=1
  for container in "$container_a" "$container_b"; do
    status=$(task_field "$container" 3 2>/dev/null || true)
    if [[ "$status" != RUNNING ]]; then
      printf 'WARN: container task is not RUNNING: %s\n' "$container" >&2
      migrated=0
      continue
    fi
    if ! ctr -n "$namespace" tasks exec \
      --exec-id "$(unique_exec_id check-hot-mount)" \
      "$container" /bin/bash -lc \
      'test -s /opt/enpu/vcann-rt/hot/libvruntime.so'; then
      printf 'WARN: %s still uses the legacy single-file runtime mount\n' "$container" >&2
      migrated=0
    fi
  done

  if ((migrated == 1)); then
    for container in "$container_a" "$container_b"; do
      ctr -n "$namespace" tasks exec \
        --exec-id "$(unique_exec_id verify-hot-runtime)" \
        "$container" /bin/bash -lc '
          set -Eeuo pipefail
          expected_hash=$1
          runtime=/opt/enpu/vcann-rt/hot/libvruntime.so
          actual_hash=$(sha256sum "$runtime" | awk "{print \$1}")
          test "$actual_hash" = "$expected_hash"
          grep -F "$runtime" /proc/self/maps >/dev/null
          echo "HOT_RUNTIME_READY container=$2 runtime_sha256=$actual_hash"
        ' _ "$runtime_sha256" "$container"
    done
    printf 'RUNTIME_REPLACE_COMPLETE runtime=%s sha256=%s\n' \
      "$runtime_artifact" "$runtime_sha256"
  else
    printf 'RUNTIME_REPLACED_RESTART_REQUIRED runtime=%s sha256=%s\n' \
      "$runtime_artifact" "$runtime_sha256"
  fi
  exit 0
fi

preload_tmp=$generated_preload.tmp.$$
python3 - /root/isa/bins/ld.so.preload "$preload_tmp" \
  "$container_runtime_dir/libvruntime.so" <<'PY'
import os
import re
import sys

source, destination, replacement = sys.argv[1:]
text = open(source, encoding="utf-8").read()
count = 0
output = []
for line in text.splitlines(keepends=True):
    parts = re.split(r"(\s+)", line)
    for index in range(0, len(parts), 2):
        token = parts[index]
        if token.startswith("#"):
            break
        if token and os.path.basename(token) == "libvruntime.so":
            parts[index] = replacement
            count += 1
    output.append("".join(parts))
if count != 1:
    raise SystemExit(f"expected exactly one libvruntime.so in {source}, found {count}")
with open(destination, "w", encoding="utf-8") as stream:
    stream.write("".join(output))
PY
chmod 0644 "$preload_tmp"
mv -f "$preload_tmp" "$generated_preload"

log 'delete exact old test containers'
delete_exact_container "$container_a"
delete_exact_container "$container_b"

create_container() {
  local container=$1
  local config=$2
  local master_port=$3
  local socket_range=$4

  ctr -n "$namespace" run \
    --env ASCEND_RUNTIME_OPTIONS=NODRV \
    --env "ENPU_DEADLOCK_TRACE=$trace_enabled" \
    --env MASTER_ADDR=localhost \
    --env "MASTER_PORT=$master_port" \
    --env "HCCL_NPU_SOCKET_PORT_RANGE=$socket_range" \
    --env HCCL_OP_EXPANSION_MODE=AIV \
    --env TASK_QUEUE_ENABLE=1 \
    --env XLITE_DISABLE_XCCL=true \
    --cap-add CAP_SYS_PTRACE \
    --detach \
    --device /dev/davinci0 --device /dev/davinci1 \
    --device /dev/davinci2 --device /dev/davinci3 \
    --device /dev/davinci4 --device /dev/davinci5 \
    --device /dev/davinci6 --device /dev/davinci7 \
    --device /dev/davinci_manager --device /dev/devmm_svm --device /dev/hisi_hdc \
    --mount type=bind,src=/usr/local/Ascend/driver/,dst=/usr/local/Ascend/driver/,options=rbind:ro \
    --mount type=bind,src=/cache/isa/Qwen3-4B,dst=/opt/model/Qwen3-4B/,options=rbind:ro \
    --mount type=bind,src=/cache/isa/Qwen3-32B,dst=/opt/model/Qwen3-32B/,options=rbind:ro \
    --mount "type=bind,src=$runtime_dir,dst=$container_runtime_dir,options=rbind:ro" \
    --mount type=bind,src=/root/isa/bins/enpu-monitor,dst=/opt/enpu/vcann-rt/tools/enpu-monitor,options=rbind:rw \
    --mount "type=bind,src=$config,dst=/etc/enpu/vcann-rt/npu_info.config,options=rbind:rw" \
    --mount "type=bind,src=$generated_preload,dst=/etc/ld.so.preload,options=bind:ro" \
    --mount type=bind,src=/usr/local/sbin/npu-smi,dst=/usr/local/sbin/npu-smi,options=rbind:ro \
    --mount type=bind,src=/usr/bin/systemd-detect-virt,dst=/usr/bin/systemd-detect-virt,options=rbind:rw \
    --mount type=bind,src=/opt/isa/shm,dst=/dev/shm,options=rbind:rw \
    --mount "type=bind,src=$repo_root,dst=$repo_root,options=rbind:rw" \
    --net-host \
    "$image" "$container" /bin/bash
}

log "create $container_a"
if ! create_container "$container_a" "$config_a" 29504 61000-61050; then
  delete_exact_container "$container_a"
  die "failed to create $container_a; removed its partial state"
fi

log "create $container_b"
if ! create_container "$container_b" "$config_b" 29510 62000-62050; then
  delete_exact_container "$container_b"
  delete_exact_container "$container_a"
  die "failed to create $container_b; removed the partial new pair"
fi

for container in "$container_a" "$container_b"; do
  status=$(task_field "$container" 3 2>/dev/null || true)
  [[ "$status" == RUNNING ]] || die "new container task is not RUNNING: $container"
done

log 'install xLite and GDB into both containers'
wheel_name=$(basename "$xlite_wheel")
for container in "$container_a" "$container_b"; do
  pid=$(task_field "$container" 2)
  [[ "$pid" =~ ^[0-9]+$ ]] || die "invalid task PID for $container: $pid"
  container_root=/proc/$pid/root
  [[ -d "$container_root/workspace" ]] || die "container workspace missing: $container"
  [[ -d "$container_root/usr/local/bin" ]] || die "container /usr/local/bin missing: $container"

  install -m 0644 "$xlite_wheel" "$container_root/workspace/$wheel_name"
  install -m 0755 "$gdb_source" "$container_root/usr/local/bin/gdb"

  ctr -n "$namespace" tasks exec \
    --exec-id "$(unique_exec_id install-tools)" \
    "$container" /bin/bash -lc '
      set -Eeuo pipefail
      wheel_name=$1
      expected_version=$2
      cd /workspace
      python3 -m pip install "./$wheel_name"
      python3 -c "import importlib.metadata as m; v=m.version(\"xlite\"); print(\"XLITE_VERSION=\" + v); assert v == \"$expected_version\""
      gdb --version | sed -n "1p"
      ldd_output=$(ldd /usr/local/bin/gdb 2>&1 || true)
      printf "%s\n" "$ldd_output"
      ! grep -F "not found" <<<"$ldd_output"
    ' _ "$wheel_name" "$expected_xlite_version"
done

log 'diagnostic preflight'
for container in "$container_a" "$container_b"; do
  ctr -n "$namespace" tasks exec \
    --exec-id "$(unique_exec_id diag-preflight)" \
    "$container" /bin/bash -lc '
      set -Eeuo pipefail
      expected_trace=$1
      expected_version=$2
      expected_runtime_sha256=$3
      runtime=/opt/enpu/vcann-rt/hot/libvruntime.so

      test "${ENPU_DEADLOCK_TRACE:-}" = "$expected_trace"
      python3 -c "import importlib.metadata as m; assert m.version(\"xlite\") == \"$expected_version\""
      command -v gdb >/dev/null
      grep -F "$runtime" /proc/self/maps >/dev/null

      mounted_hash=$(sha256sum "$runtime" | awk "{print \$1}")
      test "$expected_runtime_sha256" = "$mounted_hash"

      for symbol in \
        aclrtBinaryGetFunction \
        g_vcann_trace \
        g_vcann_sync_probe \
        g_vcann_host_sync_probe \
        g_vcann_kernel_registry \
        vcann_trace_record_enabled; do
        readelf -sW "$runtime" | grep -F " $symbol" >/dev/null
      done

      cap_eff=$(awk "/^CapEff:/ {print \$2}" /proc/self/status)
      python3 -c "v=int(\"$cap_eff\", 16); assert v & (1 << 19)"
      grep -Eq "virtual-npu-id|aicore-quota|memory-quota|scheduling-policy" \
        /etc/enpu/vcann-rt/npu_info.config
      echo "DIAGNOSTIC_PREFLIGHT_OK container=$4 runtime_sha256=$mounted_hash"
    ' _ "$trace_enabled" "$expected_xlite_version" "$runtime_sha256" "$container"
done

log 'restart complete'
ctr -n "$namespace" containers list | grep -E "^($container_a|$container_b)[[:space:]]"
ctr -n "$namespace" tasks list | grep -E "^($container_a|$container_b)[[:space:]]"
printf 'RESTART_COMPLETE runtime=%s trace=%s\n' "$runtime_artifact" "$trace_enabled"
