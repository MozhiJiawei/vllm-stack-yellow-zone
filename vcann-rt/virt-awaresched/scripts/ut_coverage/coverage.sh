#!/usr/bin/env bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# VSched is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
set -o errexit    # Exit immediately on any command failure
set -o nounset    # Treat unset variables as errors
set -o pipefail   # Catch failures in pipes

# --------------------------
# Configuration variables
# --------------------------
declare -r DEFAULT_BUILD_DIR="${PWD}/cmake-build-debug"
declare -r DEFAULT_LCOVRC="${PWD}/test/.lcovrc"
declare -r coverage_dir="${1:-${DEFAULT_BUILD_DIR}}/coverage"
declare -r FASTCOV_PATH="${HOME}/.local/bin/fastcov"
declare -A lcov_filters=([include]="" [exclude]="")

# --------------------------
# Function definitions
# --------------------------

##
# Parse include/exclude patterns from .lcovrc file
# Globals:
#   lcov_filters (output)
# Arguments:
#   $1 - path to .lcovrc configuration file
##
function parse_lcov_filters() {
    local config_file="${1}"
    while IFS= read -r line; do
        line="$(sed -E 's/^\s*|\s*$//g; s/\s*=\s*/ /' <<< "${line}")"
        [[ -z "${line}" ]] && continue

        local filter_type="${line%% *}"
        local pattern="${line#* }"

        case "${filter_type}" in
            include|exclude)
                lcov_filters["${filter_type}"]+="${pattern} "
                ;;
        esac
    done < <(grep -E '^(include|exclude)' "${config_file}")
}

##
# Generate coverage report using fastcov and genhtml
# Arguments:
#   $1 - output directory for coverage report
#   $2 - path to .lcovrc configuration file
##
function generate_coverage_report() {
    local output_dir="$1"
    local lcovrc_file="$2"

    # Validate required tools
    local genhtml_path="$(command -v genhtml)" || error_exit "genhtml not found in PATH"

    # Create clean output directory
    mkdir -p "${output_dir}"

    # Generate intermediate coverage info
    "${FASTCOV_PATH}" -b -n -p \
        --include ${lcov_filters[include]} \
        --exclude ${lcov_filters[exclude]} \
        --lcov -o "${output_dir}/fastcov.info"

    # Generate HTML report
    "${genhtml_path}" -q --config-file "${lcovrc_file}" \
        -o "${output_dir}" "${output_dir}/fastcov.info"
}

##
# Display error message and exit
# Arguments:
#   $1 - error message
##
function error_exit() {
    echo "[ERROR] $1" >&2
    exit 1
}

##
# Ensure fastcov installed
# GloGlobals::
#   FASTCOV_PATH (input)
##
ensure_fastcov_installed() {
    # Check the existence of the two installation methods (PATH and specified path)
    if ! command -v fastcov >/dev/null 2>&1 && \
       [[ ! -f "${FASTCOV_PATH}" ]]; then
        echo "Installing fastcov via pip..."
        if ! pip3 install fastcov \
                --disable-pip-version-check --user \
                --trusted-host repo.huaweicloud.com \
 	              -i https://repo.huaweicloud.com/repository/pypi/simple/ \
        ; then
            error_exit "Failed to install fastcov. Check network connection."
        fi
    fi
}

# --------------------------
# Main execution
# --------------------------
function main() {
    local lcovrc_file="${2:-${DEFAULT_LCOVRC}}"

    ensure_fastcov_installed || error_exit "Missing fastcov installed"

    # Validate configuration files
    [[ -f "${lcovrc_file}" ]] || error_exit "Missing lcovrc file: ${lcovrc_file}"
    [[ -f "${FASTCOV_PATH}" ]] || error_exit "Missing fastcov script: ${FASTCOV_PATH}"

    parse_lcov_filters "${lcovrc_file}"
    generate_coverage_report "${coverage_dir}" "${lcovrc_file}"

    echo "Coverage report generated at: ${coverage_dir}/index.html"
}

main "$@"