#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
  printf 'Usage: %s NEW_CODE_DIR OLD_CODE_DIR\n' "${0##*/}" >&2
  exit 2
}

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

[[ $# -eq 2 ]] || usage

new_dir="$1"
old_dir="$2"

[[ -d "$new_dir" ]] || fail "new code directory not found: $new_dir"
[[ -d "$old_dir" ]] || fail "old code directory not found: $old_dir"

new_root="$(git -C "$new_dir" rev-parse --show-toplevel)" || fail "new code is not in a Git repository"
old_root="$(git -C "$old_dir" rev-parse --show-toplevel)" || fail "old code is not in a Git repository"
new_prefix="$(git -C "$new_dir" rev-parse --show-prefix)"
old_prefix="$(git -C "$old_dir" rev-parse --show-prefix)"

readonly new_dir old_dir new_root old_root new_prefix old_prefix

pathspec_for() {
  local prefix="$1"
  if [[ -n "$prefix" ]]; then
    printf '%s\n' "${prefix%/}"
  else
    printf '.\n'
  fi
}

require_clean_path() {
  local label="$1"
  local root="$2"
  local prefix="$3"
  local changes

  changes="$(git -C "$root" status --porcelain --untracked-files=normal -- "$(pathspec_for "$prefix")")"
  if [[ -n "$changes" ]]; then
    printf 'ERROR: %s contains changes not represented by HEAD:\n%s\n' "$label" "$changes" >&2
    exit 1
  fi
}

snapshot_head() {
  local root="$1"
  local prefix="$2"
  local destination="$3"
  local component_count

  mkdir -p "$destination"
  if [[ -z "$prefix" ]]; then
    git -C "$root" archive --format=tar HEAD | tar -xf - -C "$destination"
    return
  fi

  component_count="$(awk -F/ '{ print NF }' <<<"${prefix%/}")"
  git -C "$root" archive --format=tar HEAD "${prefix%/}" \
    | tar -xf - -C "$destination" --strip-components="$component_count"
}

require_clean_path "new code directory" "$new_root" "$new_prefix"
require_clean_path "old code directory" "$old_root" "$old_prefix"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
output_dir="$script_dir/.vcann-sync"
old_snapshot="$output_dir/old"
new_snapshot="$output_dir/new"

rm -rf "$output_dir"
mkdir -p "$old_snapshot" "$new_snapshot"

snapshot_head "$old_root" "$old_prefix" "$old_snapshot"
snapshot_head "$new_root" "$new_prefix" "$new_snapshot"

diff_status=0
git diff --no-index --binary --no-renames \
  --src-prefix=a/vcann-rt/ --dst-prefix=b/vcann-rt/ \
  "$old_snapshot" "$new_snapshot" >"$output_dir/full.patch" || diff_status=$?
[[ $diff_status -le 1 ]] || fail "git diff failed with status $diff_status"

git diff --no-index --stat --no-renames \
  "$old_snapshot" "$new_snapshot" >"$output_dir/stat.txt" || true
git diff --no-index --name-status --no-renames \
  "$old_snapshot" "$new_snapshot" >"$output_dir/name-status.txt" || true

{
  printf 'new_root=%s\n' "$new_root"
  printf 'new_prefix=%s\n' "$new_prefix"
  printf 'new_head=%s\n' "$(git -C "$new_root" rev-parse HEAD)"
  printf 'old_root=%s\n' "$old_root"
  printf 'old_prefix=%s\n' "$old_prefix"
  printf 'old_head=%s\n' "$(git -C "$old_root" rev-parse HEAD)"
  printf 'different=%s\n' "$([[ $diff_status -eq 1 ]] && printf yes || printf no)"
} >"$output_dir/metadata.txt"

rm -rf "$old_snapshot" "$new_snapshot"

cat "$output_dir/metadata.txt"
printf '\n===== DIFF STAT =====\n'
cat "$output_dir/stat.txt"
printf '\nReports written to %s\n' "$output_dir"

