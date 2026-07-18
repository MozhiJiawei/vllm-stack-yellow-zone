#!/usr/bin/env bash

set -Eeuo pipefail

if [[ $# -ne 3 ]]; then
  printf 'Usage: %s PERSONAL_DIR MASTER_DIR OLD_DIR\n' "${0##*/}" >&2
  exit 2
fi

personal_dir="$1"
master_dir="$2"
old_dir="$3"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
compare="$script_dir/compare-unrelated-vcann.sh"

rm -rf "$script_dir/.vcann-sync"

printf '===== PERSONAL VS OLD =====\n'
REPORT_NAME=personal-vs-old "$compare" "$personal_dir" "$old_dir"

printf '\n===== MASTER VS OLD =====\n'
REPORT_NAME=master-vs-old "$compare" "$master_dir" "$old_dir"

printf '\n===== PERSONAL VS MASTER =====\n'
REPORT_NAME=personal-vs-master "$compare" "$personal_dir" "$master_dir"

printf '\nAll reports written under %s/.vcann-sync\n' "$script_dir"
