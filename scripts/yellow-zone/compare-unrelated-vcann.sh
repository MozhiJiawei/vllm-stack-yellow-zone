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

detect_source() {
  local side="$1"
  local directory="$2"
  local source root prefix revision

  if root="$(git -C "$directory" rev-parse --show-toplevel 2>/dev/null)"; then
    source=git
    prefix="$(git -C "$directory" rev-parse --show-prefix)"
    revision="$(git -C "$root" rev-parse HEAD)"
  else
    source=filesystem
    root="$(cd -- "$directory" && pwd -P)"
    prefix=
    revision=filesystem
  fi

  printf -v "${side}_source" '%s' "$source"
  printf -v "${side}_root" '%s' "$root"
  printf -v "${side}_prefix" '%s' "$prefix"
  printf -v "${side}_revision" '%s' "$revision"
}

detect_source new "$new_dir"
detect_source old "$old_dir"

readonly new_dir old_dir
readonly new_source new_root new_prefix new_revision
readonly old_source old_root old_prefix old_revision

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

if [[ "$new_source" == git ]]; then
  require_clean_path "new code directory" "$new_root" "$new_prefix"
fi
if [[ "$old_source" == git ]]; then
  require_clean_path "old code directory" "$old_root" "$old_prefix"
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
report_name="${REPORT_NAME:-}"
[[ -z "$report_name" || "$report_name" =~ ^[A-Za-z0-9._-]+$ ]] \
  || fail "REPORT_NAME may contain only letters, digits, dot, underscore, and dash"

output_dir="$script_dir/.vcann-sync"
[[ -z "$report_name" ]] || output_dir="$output_dir/$report_name"
object_repo="$output_dir/objects.git"

rm -rf "$output_dir"
mkdir -p "$output_dir"

git init --quiet --bare "$object_repo"
git -C "$object_repo" config core.autocrlf input

prepare_tree() {
  local side="$1"
  local source="$2"
  local root="$3"
  local prefix="$4"
  local index tree

  if [[ "$source" == git ]]; then
    git -C "$object_repo" fetch --quiet --no-tags "$root" "HEAD:refs/heads/$side"
    tree="refs/heads/$side^{tree}"
    [[ -z "$prefix" ]] || tree="refs/heads/$side:${prefix%/}"
    printf '%s\n' "$tree"
    return
  fi

  index="$output_dir/$side.index"
  GIT_INDEX_FILE="$index" git --git-dir="$object_repo" --work-tree="$root" read-tree --empty
  GIT_INDEX_FILE="$index" git --git-dir="$object_repo" --work-tree="$root" add --all -- .
  GIT_INDEX_FILE="$index" git --git-dir="$object_repo" write-tree
}

old_tree="$(prepare_tree old "$old_source" "$old_root" "$old_prefix")"
new_tree="$(prepare_tree new "$new_source" "$new_root" "$new_prefix")"

diff_status=0
git -C "$object_repo" diff --quiet "$old_tree" "$new_tree" || diff_status=$?
[[ $diff_status -le 1 ]] || fail "git diff failed with status $diff_status"

git -C "$object_repo" diff --binary --no-renames \
  --src-prefix=a/vcann-rt/ --dst-prefix=b/vcann-rt/ \
  "$old_tree" "$new_tree" \
  >"$output_dir/full.patch"

git -C "$object_repo" diff --stat --no-renames \
  "$old_tree" "$new_tree" >"$output_dir/stat.txt"
git -C "$object_repo" diff --name-status --no-renames \
  "$old_tree" "$new_tree" >"$output_dir/name-status.txt"

{
  printf 'new_root=%s\n' "$new_root"
  printf 'new_source=%s\n' "$new_source"
  printf 'new_prefix=%s\n' "$new_prefix"
  printf 'new_revision=%s\n' "$new_revision"
  printf 'old_root=%s\n' "$old_root"
  printf 'old_source=%s\n' "$old_source"
  printf 'old_prefix=%s\n' "$old_prefix"
  printf 'old_revision=%s\n' "$old_revision"
  printf 'different=%s\n' "$([[ $diff_status -eq 1 ]] && printf yes || printf no)"
} >"$output_dir/metadata.txt"

rm -rf "$object_repo"
rm -f "$output_dir/new.index" "$output_dir/old.index"

cat "$output_dir/metadata.txt"
printf '\n===== DIFF STAT =====\n'
cat "$output_dir/stat.txt"
printf '\nReports written to %s\n' "$output_dir"
