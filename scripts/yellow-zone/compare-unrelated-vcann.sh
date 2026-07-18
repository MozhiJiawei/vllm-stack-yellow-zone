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

old_root="$(git -C "$old_dir" rev-parse --show-toplevel)" || fail "old code is not in a Git repository"
old_prefix="$(git -C "$old_dir" rev-parse --show-prefix)"

if new_root="$(git -C "$new_dir" rev-parse --show-toplevel 2>/dev/null)"; then
  new_source=git
  new_prefix="$(git -C "$new_dir" rev-parse --show-prefix)"
  new_revision="$(git -C "$new_root" rev-parse HEAD)"
else
  new_source=filesystem
  new_root="$(cd -- "$new_dir" && pwd -P)"
  new_prefix=
  new_revision=filesystem
fi

readonly new_dir old_dir new_source new_root old_root new_prefix old_prefix new_revision

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

require_clean_path "old code directory" "$old_root" "$old_prefix"
if [[ "$new_source" == git ]]; then
  require_clean_path "new code directory" "$new_root" "$new_prefix"
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
output_dir="$script_dir/.vcann-sync"
object_repo="$output_dir/objects.git"
new_index="$output_dir/new.index"

rm -rf "$output_dir"
mkdir -p "$output_dir"

git init --quiet --bare "$object_repo"
git -C "$object_repo" fetch --quiet --no-tags "$old_root" HEAD:refs/heads/old

old_tree="refs/heads/old^{tree}"
[[ -z "$old_prefix" ]] || old_tree="refs/heads/old:${old_prefix%/}"

if [[ "$new_source" == git ]]; then
  git -C "$object_repo" fetch --quiet --no-tags "$new_root" HEAD:refs/heads/new
  new_tree="refs/heads/new^{tree}"
  [[ -z "$new_prefix" ]] || new_tree="refs/heads/new:${new_prefix%/}"
else
  git -C "$object_repo" config core.autocrlf false
  GIT_INDEX_FILE="$new_index" git --git-dir="$object_repo" --work-tree="$new_root" read-tree --empty
  GIT_INDEX_FILE="$new_index" git --git-dir="$object_repo" --work-tree="$new_root" add --all -- .
  new_tree="$(GIT_INDEX_FILE="$new_index" git --git-dir="$object_repo" write-tree)"
fi

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
  printf 'old_prefix=%s\n' "$old_prefix"
  printf 'old_head=%s\n' "$(git -C "$old_root" rev-parse HEAD)"
  printf 'different=%s\n' "$([[ $diff_status -eq 1 ]] && printf yes || printf no)"
} >"$output_dir/metadata.txt"

rm -rf "$object_repo"

cat "$output_dir/metadata.txt"
printf '\n===== DIFF STAT =====\n'
cat "$output_dir/stat.txt"
printf '\nReports written to %s\n' "$output_dir"
