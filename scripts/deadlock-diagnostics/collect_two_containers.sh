#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: collect_two_containers.sh --container-a NAME --container-b NAME \
  --run-id ID --output DIR [--diag-dir DIR] [--runtime docker|podman] [--no-freeze]

The script copies deadlock_snapshot.py into each already-running container,
captures both TP8 worker groups concurrently, and leaves workers frozen unless
--no-freeze is supplied. It does not start services or workloads.
EOF
}

container_a=
container_b=
run_id=
output=
diag_dir=/tmp/vllm-deadlock-diag
runtime=docker
freeze=1

while (($#)); do
  case "$1" in
    --container-a) container_a=$2; shift 2 ;;
    --container-b) container_b=$2; shift 2 ;;
    --run-id) run_id=$2; shift 2 ;;
    --output) output=$2; shift 2 ;;
    --diag-dir) diag_dir=$2; shift 2 ;;
    --runtime) runtime=$2; shift 2 ;;
    --no-freeze) freeze=0; shift ;;
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
if ! command -v "$runtime" >/dev/null 2>&1; then
  echo "container runtime not found: $runtime" >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
collector=$script_dir/deadlock_snapshot.py
container_collector=/tmp/vllm-deadlock-snapshot.py
container_root=/tmp/vllm-deadlock-capture-$run_id
mkdir -p -- "$output"

for container in "$container_a" "$container_b"; do
  "$runtime" cp "$collector" "$container:$container_collector"
done

freeze_arg=()
if ((freeze)); then
  freeze_arg=(--freeze)
fi

"$runtime" exec "$container_a" python3 "$container_collector" capture \
  --diag-dir "$diag_dir" --run-id "$run_id" --model A --expected-workers 8 \
  --output "$container_root/model-A" "${freeze_arg[@]}" >"$output/model-A.capture.log" 2>&1 &
pid_a=$!
"$runtime" exec "$container_b" python3 "$container_collector" capture \
  --diag-dir "$diag_dir" --run-id "$run_id" --model B --expected-workers 8 \
  --output "$container_root/model-B" "${freeze_arg[@]}" >"$output/model-B.capture.log" 2>&1 &
pid_b=$!

status_a=0
status_b=0
wait "$pid_a" || status_a=$?
wait "$pid_b" || status_b=$?
if ((status_a != 0 || status_b != 0)); then
  echo "capture failed: model-A=$status_a model-B=$status_b" >&2
  echo "inspect $output/model-A.capture.log and $output/model-B.capture.log" >&2
  exit 2
fi

"$runtime" cp "$container_a:$container_root/model-A" "$output/model-A"
"$runtime" cp "$container_b:$container_root/model-B" "$output/model-B"
python3 "$collector" summarize \
  --capture-dir "$output/model-A" --capture-dir "$output/model-B" --output "$output"

tar -C "$(dirname -- "$output")" -czf "$output.tar.gz" "$(basename -- "$output")"
echo "capture archive: $output.tar.gz"
if ((freeze)); then
  echo "workers remain frozen; run deadlock_snapshot.py resume inside each container when ready"
fi
