#!/usr/bin/env bash
set -e
# search all modified files (pre-commit input)
for file in "$@"; do
  # find root dir
  dir=$(dirname "$file")

  #find build dir
  while [[ "$dir" != "/" && ! -d "$dir/build" ]]; do
    dir=$(dirname "$dir")
  done

  if [[ ! -d "$dir/build" ]]; then
    echo "skip $file: build dir not found"
  fi

  echo "check $file"
  echo "use database: $dir/build/compile_commands.json"

  #run clang-tidy
  clang-tidy \
    -p="$dir/build" \
    -header-filter=.* \
    "$file"
done