#!/usr/bin/env bash
# Collect only the host/container facts needed to run the two-container XCCL
# control plane through ctr. This script is read-only and targets two explicitly
# named containers; it does not inspect NPU, driver, model, or package state.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: collect-ctr-two-container-env.sh \
  --container-a ID --container-b ID [--namespace NAME]

Defaults:
  --namespace k8s.io
EOF
}

namespace=k8s.io
container_a=
container_b=

while (($#)); do
  case "$1" in
    --container-a)
      container_a=${2:-}
      shift 2
      ;;
    --container-b)
      container_b=${2:-}
      shift 2
      ;;
    --namespace)
      namespace=${2:-}
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

if [[ -z "$container_a" || -z "$container_b" || -z "$namespace" ]]; then
  printf 'ERROR: --container-a, --container-b, and --namespace must be non-empty\n' >&2
  usage >&2
  exit 2
fi
if [[ "$container_a" == "$container_b" ]]; then
  printf 'ERROR: container A and B must be different\n' >&2
  exit 2
fi

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'ERROR: required command not found: %s\n' "$1" >&2
    exit 2
  fi
}

require_command ctr
require_command python3
require_command readlink
require_command sed

print_container_metadata() {
  local container=$1
  local container_json=$2
  local task_json=$3

  CONTAINER_ID="$container" python3 -c '
import json
import os
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    container = json.load(source)
with open(sys.argv[2], encoding="utf-8") as source:
    task = json.load(source)

def pick(mapping, *names, default=None):
    if not isinstance(mapping, dict):
        return default
    lowered = {str(key).lower(): value for key, value in mapping.items()}
    for name in names:
        if name.lower() in lowered:
            return lowered[name.lower()]
    return default

spec = pick(container, "Spec", default={}) or {}
process = pick(spec, "process", default={}) or {}
runtime = pick(container, "Runtime", default={}) or {}
linux = pick(spec, "linux", default={}) or {}
namespaces = pick(linux, "namespaces", default=[]) or []
mounts = pick(spec, "mounts", default=[]) or []

container_id = os.environ["CONTAINER_ID"]
image = pick(container, "Image", default="unknown")
runtime_name = pick(runtime, "Name", default="unknown")
task_pid = pick(task, "Pid", default="unknown")
task_status = pick(task, "Status", default="unknown")
container_cwd = pick(process, "cwd", default="unknown")
print(f"container_id={container_id}")
print(f"image={image}")
print(f"runtime={runtime_name}")
print(f"task_pid={task_pid}")
print(f"task_status={task_status}")
print(f"container_cwd={container_cwd}")

network_types = {
    str(pick(item, "type", default="")).lower()
    for item in namespaces
    if isinstance(item, dict)
}
isolated = any(value in {"network", "network_namespace"} for value in network_types)
network_namespace = "isolated" if isolated else "host_or_unspecified"
print(f"oci_network_namespace={network_namespace}")

project_mounts = []
for mount in mounts:
    source = str(pick(mount, "source", default=""))
    destination = str(pick(mount, "destination", default=""))
    if "/root/l00933108" in source or "/root/l00933108" in destination:
        project_mounts.append(f"{source}->{destination}")
print("project_mounts=" + (";".join(project_mounts) if project_mounts else "none"))
' <(printf '%s' "$container_json") <(printf '%s' "$task_json")
}

inspect_role() {
  local role=$1
  local container=$2
  local container_json
  local task_json
  local pid
  local host_net
  local exec_prefix="xccl-env-${role,,}-$$"

  if ! container_json=$(ctr -n "$namespace" containers info "$container" 2>&1); then
    printf 'ERROR: cannot inspect role=%s container=%s\n%s\n' \
      "$role" "$container" "$container_json" >&2
    exit 2
  fi
  if ! task_json=$(ctr -n "$namespace" tasks info "$container" 2>&1); then
    printf 'ERROR: role=%s container=%s has no inspectable running task\n%s\n' \
      "$role" "$container" "$task_json" >&2
    exit 2
  fi

  printf '=== role=%s selected container ===\n' "$role"
  print_container_metadata "$container" "$container_json" "$task_json"

  pid=$(printf '%s' "$task_json" | python3 -c '
import json
import sys
data = json.load(sys.stdin)
values = {str(key).lower(): value for key, value in data.items()}
print(values.get("pid", ""))
')
  if [[ ! "$pid" =~ ^[0-9]+$ ]] || [[ ! -e "/proc/$pid/ns/net" ]]; then
    printf 'ERROR: invalid or unavailable task PID for role=%s: %s\n' \
      "$role" "${pid:-<empty>}" >&2
    exit 2
  fi
  if [[ "$(readlink /proc/1/ns/net)" == "$(readlink "/proc/$pid/ns/net")" ]]; then
    host_net=yes
  else
    host_net=no
  fi
  printf 'host_network=%s\n' "$host_net"

  ctr -n "$namespace" tasks exec \
    --exec-id "${exec_prefix}-runtime" \
    "$container" /bin/sh -lc '
      printf "container_python="
      command -v python3 || printf "missing\n"
      if command -v python3 >/dev/null 2>&1; then
        python3 --version
      fi
      printf "container_identity="
      id -u
      printf "container_pwd="
      pwd
      for path in /tmp /root; do
        if test -d "$path" && test -w "$path"; then
          printf "writable_%s=yes\n" "${path#/}"
        else
          printf "writable_%s=no\n" "${path#/}"
        fi
      done
      printf "container_tar="
      command -v tar || printf "missing\n"
      if command -v ip >/dev/null 2>&1; then
        printf "default_route="
        ip route show default | head -n 1
      else
        printf "default_route=ip-command-missing\n"
      fi
    '

  printf 'ctr-stdin-ok\n' | ctr -n "$namespace" tasks exec \
    --exec-id "${exec_prefix}-stdin" \
    "$container" /bin/sh -c '
      IFS= read -r probe
      test "$probe" = ctr-stdin-ok
      printf "ctr_exec_stdin=ok\n"
    '
}

printf '=== collection scope ===\n'
printf 'namespace=%s\ncontainer_a=%s\ncontainer_b=%s\n' \
  "$namespace" "$container_a" "$container_b"

printf '=== ctr/containerd ===\n'
ctr version | sed -n '1,6p'
printf 'host_python=%s\n' "$(command -v python3)"
python3 --version

inspect_role A "$container_a"
inspect_role B "$container_b"

printf 'RESULT=COLLECTION_COMPLETE\n'
