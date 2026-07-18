#!/usr/bin/env bash
# Run the xLite aligned/crossed A/B experiment and save one complete log file.

set -uo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
output_file=${1:-"$PWD/tp8-xlite-ab-control-latest.log"}
case "$output_file" in
  /*) ;;
  *) output_file="$PWD/$output_file" ;;
esac

# A fixed default filename makes repeated remote collection convenient. Passing
# an explicit first argument preserves a run under a different filename.
: >"$output_file" || {
  printf 'ERROR: cannot create result file: %s\n' "$output_file" >&2
  exit 2
}
exec 3>&1
printf 'Writing complete experiment output to: %s\n' "$output_file" >&3
exec >>"$output_file" 2>&1

unset HCCL_DETERMINISTIC
unset HCCL_OP_EXPANSION_MODE
export XLITE_DISABLE_XCCL=false
export ASCEND_SLOG_PRINT_TO_STDOUT=0
export ASCEND_GLOBAL_LOG_LEVEL=3

printf 'result_file=%s\n' "$output_file"
printf 'started_at=%s\n' "$(date --iso-8601=seconds)"

run_case() {
  local case_name=$1
  shift

  printf '\n===== CASE=%s =====\n' "$case_name"
  python3 "$script_dir/repro-tp8-xlite-xccl-deadlock.py" \
    --decode-tokens 256 \
    --hidden-size 5120 \
    --dtype bf16 \
    --init-stagger-seconds 10 \
    --stagger-seconds 5 \
    --hang-timeout 30 \
    "$@"
  local case_rc=$?
  printf 'CASE_RESULT name=%s exit_code=%d\n' "$case_name" "$case_rc"
  return "$case_rc"
}

run_case aligned --schedule aligned
aligned_rc=$?

run_case crossed --schedule crossed
crossed_rc=$?

printf '\n===== FINAL SUMMARY =====\n'
printf 'aligned_exit_code=%d\n' "$aligned_rc"
printf 'crossed_exit_code=%d\n' "$crossed_rc"
printf 'finished_at=%s\n' "$(date --iso-8601=seconds)"
printf 'result_file=%s\n' "$output_file"
printf \
  'Experiment finished: aligned_exit_code=%d crossed_exit_code=%d file=%s\n' \
  "$aligned_rc" "$crossed_rc" "$output_file" >&3

if ((aligned_rc == 2 || crossed_rc == 2)); then
  exit 2
fi
if ((aligned_rc != 0)); then
  exit 1
fi
exit 0
