#!/usr/bin/env bash

set -Eeuo pipefail

GRACE_SECONDS="${GRACE_SECONDS:-10}"
signal="TERM"
dry_run=0

usage() {
  cat <<'EOF'
Usage: stop-vllm-processes.sh [--force] [--dry-run]

Stop every local vLLM process and its descendants.

Options:
  --force    Send SIGKILL immediately instead of waiting for SIGTERM.
  --dry-run  Print matching processes without sending a signal.
  -h, --help Show this help text.

Environment:
  GRACE_SECONDS  Seconds to wait before SIGKILL (default: 10).
EOF
}

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

while (($# > 0)); do
  case "$1" in
    --force)
      signal="KILL"
      ;;
    --dry-run)
      dry_run=1
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      fail "unknown argument: $1"
      ;;
  esac
  shift
done

[[ "$GRACE_SECONDS" =~ ^[0-9]+$ ]] || \
  fail "GRACE_SECONDS must be a non-negative integer"

for command in ps kill sleep sort; do
  command -v "$command" >/dev/null 2>&1 || \
    fail "required command not found: $command"
done

process_is_running() {
  local state
  state="$(ps -o stat= -p "$1" 2>/dev/null)" || return 1
  state="${state#"${state%%[![:space:]]*}"}"
  [[ -n "$state" && "$state" != Z* ]]
}

declare -A process_ppid=()
declare -A process_command=()
declare -A process_args=()
declare -A selected=()

process_snapshot="$(ps -eo pid=,ppid=,comm=,args=)" || \
  fail "could not read the process table (a procps-compatible ps is required)"

while read -r pid ppid command args; do
  [[ "$pid" =~ ^[0-9]+$ && "$ppid" =~ ^[0-9]+$ ]] || continue
  process_ppid["$pid"]="$ppid"
  process_command["$pid"]="$command"
  process_args["$pid"]="${args:-}"

  if [[ "$command" == VLLM::* ]] ||
    [[ "${args:-}" =~ (^|[[:space:]/])vllm([[:space:]/.]|$) ]]; then
    selected["$pid"]=1
  fi
done <<<"$process_snapshot"

# Include helpers such as multiprocessing.resource_tracker even though their
# command lines do not contain "vllm".
changed=1
while ((changed)); do
  changed=0
  for pid in "${!process_ppid[@]}"; do
    ppid="${process_ppid[$pid]}"
    if [[ -z "${selected[$pid]:-}" && -n "${selected[$ppid]:-}" ]]; then
      selected["$pid"]=1
      changed=1
    fi
  done
done

# Never signal the script itself, even if a caller supplied an unusual argv[0].
unset 'selected[$$]'

pids=()
if ((${#selected[@]} > 0)); then
  mapfile -t pids < <(printf '%s\n' "${!selected[@]}" | sort -n)
fi
if ((${#pids[@]} == 0)); then
  printf 'No vLLM processes found.\n'
  exit 0
fi

printf '%-8s %-8s %-24s %s\n' PID PPID COMMAND ARGS
for pid in "${pids[@]}"; do
  printf '%-8s %-8s %-24s %s\n' \
    "$pid" "${process_ppid[$pid]}" "${process_command[$pid]}" \
    "${process_args[$pid]}"
done

if ((dry_run)); then
  printf '\nDry run: %d process(es) matched.\n' "${#pids[@]}"
  exit 0
fi

printf '\nSending SIG%s to %d process(es)...\n' "$signal" "${#pids[@]}"
kill "-$signal" "${pids[@]}" 2>/dev/null || true

if [[ "$signal" == "TERM" ]]; then
  for ((elapsed = 0; elapsed < GRACE_SECONDS; elapsed++)); do
    remaining=()
    for pid in "${pids[@]}"; do
      process_is_running "$pid" && remaining+=("$pid")
    done
    ((${#remaining[@]} == 0)) && break
    sleep 1
  done

  remaining=()
  for pid in "${pids[@]}"; do
    process_is_running "$pid" && remaining+=("$pid")
  done
  if ((${#remaining[@]} > 0)); then
    printf 'Sending SIGKILL to %d process(es) still running: %s\n' \
      "${#remaining[@]}" "${remaining[*]}"
    kill -KILL "${remaining[@]}" 2>/dev/null || true
  fi
fi

sleep 1
remaining=()
for pid in "${pids[@]}"; do
  process_is_running "$pid" && remaining+=("$pid")
done
if ((${#remaining[@]} > 0)); then
  fail "failed to stop process(es): ${remaining[*]}"
fi

printf 'Done.\n'
