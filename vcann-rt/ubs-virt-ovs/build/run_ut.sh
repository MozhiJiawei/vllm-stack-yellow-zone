#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR="$ROOT_DIR/buildUT"
COVERAGE=true

log() {
  echo "[INFO] $*"
}
err() {
  echo "[ERROR] $*" >&2
}

extract_dep(){
  local archive="$1"
  local target_dir="$2"
  log "Extracting $archive -> $target_dir"
  rm -rf "$target_dir"
  mkdir -p "$target_dir"
  tar -xzf "$archive" -C "$target_dir" --strip-components=1 || {
    err "Falied to extract $archive"
    exit 1
  }
}

rm -rf "$BUILD_DIR"


if $COVERAGE; then
  log "config with COVERAGE ON"
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DBUILD_TESTS=ON -DCOVERAGE=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_LINKER_FLAGS="-fsanitize=address" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
fi

log "Building UT"
cmake --build "$BUILD_DIR" -j"$(nproc)"

log "Running UT"
cd "$BUILD_DIR/test"

log "Gen report"
cmake --build "$BUILD_DIR" --target coverage
coverageInfo="$BUILD_DIR/src/coverage.info"
coverageXml="$BUILD_DIR/src/coverage/test_detail.xml"
coverageHtml="$BUILD_DIR/src/coverage_html"
REPORT_DIR="$ROOT_DIR/gcover_report"

rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR"
cp "$coverageInfo" "$REPORT_DIR/"
cp "$coverageXml" "$REPORT_DIR/"
cp -r "$coverageHtml" "$REPORT_DIR/"
log "Coverage and test report files are ready"
