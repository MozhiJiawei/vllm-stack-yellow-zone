#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
# ubs-virt-enpu is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -----------------------------------------------------------------------------------------------------------

set -e

if ! command -v lcov > /dev/null 2>&1; then
    echo "[ERROR] Command lcov not found."
    exit 1
fi

echo "[DEBUG] Generating coverage report..."
echo "[DEBUG] $(pwd)"

COVERAGE_THRESHOLD=80.0

get_lcov_version() {
    local version_output
    version_output=$(lcov --version 2>/dev/null | head -n1)
    if [[ $version_output =~ ([0-9]+\.[0-9]+) ]]; then
        echo "${BASH_REMATCH[1]}"
    else
        echo "0.0"
    fi
}

version_ge() {
    [ "$(echo -e "$1\n$2" | sort -V | tail -n1)" = "$1" ]
}

lcov_version=$(get_lcov_version)
echo "get lcov version: $lcov_version"

if version_ge "$lcov_version" "2.0"; then
    echo "lcov $lcov_version supports --ignore-errors mismatch"
    IGNORED_ERRORS="mismatch,unused,unused"
else
    echo "lcov $lcov_version do not support --ignore-errors mismatch"
    IGNORED_ERRORS="gcov,source,graph"
fi

lcov --capture --directory . \
    --ignore-errors "$IGNORED_ERRORS" \
    --output-file coverage_base.info
lcov --remove coverage_base.info \
    '/usr/*' '*/test/*' '*/build/*' '*/__build/*' \
    '*.h' '*.hpp' '*.hh' '*.hxx' \
    --ignore-errors "$IGNORED_ERRORS" \
    --output-file coverage_filtered.info

echo "[DEBUG] Generating html report..."
if command -v genhtml > /dev/null 2>&1; then
    genhtml coverage_filtered.info --output-directory coverage_report
    echo "[DEBUG] Coverage html report generated to $(pwd)/coverage_report."
else
    echo "[ERROR] Command genhtml not found."
fi

LINE_COVERAGE=$(lcov --summary coverage_filtered.info \
    --ignore-errors "$IGNORED_ERRORS" \
    2>/dev/null \
    | grep "lines" | sed -E "s#.*: *([0-9]+\.?[0-9]*)% .*#\1#" \
    || echo "0.0")

if [ -n "$LINE_COVERAGE" ]; then
    if (( $(echo "$LINE_COVERAGE >= $COVERAGE_THRESHOLD" | bc -l) )); then
        echo "[INFO] Coverage check PASSED: $LINE_COVERAGE% >= $COVERAGE_THRESHOLD%."
    else
        echo "[ERROR] Coverage check FAILED: $LINE_COVERAGE% < $COVERAGE_THRESHOLD%."
        exit 1
    fi
else
    echo "[DEBUG] Error: failed to get line coverage."
    exit 1
fi

echo "[DEBUG] Done."
