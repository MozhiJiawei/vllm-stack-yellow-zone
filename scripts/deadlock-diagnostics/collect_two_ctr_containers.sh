#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: collect_two_ctr_containers.sh --container-a ID --container-b ID \
  --run-id ID --output DIR [--namespace NAME] [--expected-processes N] \
  [--pid-a PID[,PID...]] [--pid-b PID[,PID...]] [--resume-after]

Copies the vCANN-only collector into two running ctr tasks, freezes and captures
both process groups concurrently, copies the results to the host, and writes a
merged summary. The workload must have ENPU_DEADLOCK_TRACE=1 from process start.
EOF
}

container_a=
container_b=
run_id=
output=
namespace=default
expected=8
pid_a=
pid_b=
resume_after=0

while (($#)); do
  case "$1" in
    --container-a) container_a=$2; shift 2 ;;
    --container-b) container_b=$2; shift 2 ;;
    --run-id) run_id=$2; shift 2 ;;
    --output) output=$2; shift 2 ;;
    --namespace) namespace=$2; shift 2 ;;
    --expected-processes) expected=$2; shift 2 ;;
    --pid-a) pid_a=$2; shift 2 ;;
    --pid-b) pid_b=$2; shift 2 ;;
    --resume-after) resume_after=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z $container_a || -z $container_b || -z $run_id || -z $output ]]; then
  usage >&2
  exit 2
fi
if [[ ! $run_id =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]; then
  echo "invalid run ID: $run_id" >&2
  exit 2
fi
if [[ ! $expected =~ ^[1-9][0-9]*$ ]]; then
  echo "expected process count must be positive: $expected" >&2
  exit 2
fi
for list in "$pid_a" "$pid_b"; do
  if [[ -n $list && ! $list =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]]; then
    echo "invalid PID list: $list" >&2
    exit 2
  fi
done
if ! command -v ctr >/dev/null 2>&1; then
  echo "ctr is not installed" >&2
  exit 2
fi
if [[ -e $output ]]; then
  echo "output already exists: $output" >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
collector=$script_dir/collect_vcann_deadlock.py
gdb_helper=$script_dir/vcann_trace_gdb.py
container_collector=/tmp/collect-vcann-deadlock.py
container_gdb_helper=/tmp/vcann_trace_gdb.py
container_root=/tmp/vcann-deadlock-$run_id
mkdir -p -- "$output"

ctr_exec() {
  local container=$1
  local exec_id=$2
  shift 2
  ctr -n "$namespace" tasks exec --exec-id "$exec_id" "$container" "$@"
}

copy_into() {
  local container=$1
  local source=$2
  local destination=$3
  local exec_id=$4
  # shellcheck disable=SC2016 # $1 is intentionally expanded by the container shell.
  ctr_exec "$container" "$exec_id" sh -c 'umask 077; cat > "$1"; chmod 700 "$1"' sh "$destination" <"$source"
}

for item in "$container_a:A" "$container_b:B"; do
  container=${item%:*}
  model=${item##*:}
  ctr -n "$namespace" tasks info "$container" >/dev/null
  copy_into "$container" "$collector" "$container_collector" "vcann-copy-collector-$model-$run_id"
  copy_into "$container" "$gdb_helper" "$container_gdb_helper" "vcann-copy-gdb-$model-$run_id"
done

capture_one() {
  local container=$1
  local model=$2
  local pid_list=$3
  local -a command=(
    python3 "$container_collector" capture
    --model "$model"
    --expected-processes "$expected"
    --output "$container_root/model-$model"
  )
  if [[ -n $pid_list ]]; then
    local pid
    IFS=, read -ra values <<<"$pid_list"
    for pid in "${values[@]}"; do
      command+=(--pid "$pid")
    done
  fi
  if ((resume_after)); then
    command+=(--resume-after)
  fi
  ctr_exec "$container" "vcann-capture-$model-$run_id" "${command[@]}"
}

capture_one "$container_a" A "$pid_a" >"$output/model-A.capture.log" 2>&1 &
capture_a=$!
capture_one "$container_b" B "$pid_b" >"$output/model-B.capture.log" 2>&1 &
capture_b=$!

status_a=0
status_b=0
wait "$capture_a" || status_a=$?
wait "$capture_b" || status_b=$?

copy_out() {
  local container=$1
  local model=$2
  ctr_exec "$container" "vcann-copy-out-$model-$run_id" \
    tar -C "$container_root" -cf - "model-$model" | tar -C "$output" -xf -
}

copy_out "$container_a" A
copy_out "$container_b" B
python3 "$collector" summarize \
  --capture-dir "$output/model-A" --capture-dir "$output/model-B" --output "$output"
tar -C "$(dirname -- "$output")" -czf "$output.tar.gz" "$(basename -- "$output")"

echo "capture archive: $output.tar.gz"
if ((!resume_after)); then
  echo "workload processes remain frozen in both containers"
fi
if ((status_a != 0 || status_b != 0)); then
  echo "capture incomplete: model-A=$status_a model-B=$status_b" >&2
  echo "partial artifacts were preserved; inspect the two capture logs" >&2
  exit 2
fi
