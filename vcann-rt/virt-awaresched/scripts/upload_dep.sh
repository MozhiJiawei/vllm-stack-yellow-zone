#!/bin/bash
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

set -e

# Check the number of parameters
if [[ "$#" -lt 1 ]]; then
    echo "Usage: $0 <dep_name>"
    exit 1
fi

# Obtain Parameters
dep_name=$1

# Path to obtain the script
if [[ -n "$BASH_SOURCE" ]]; then
    SCRIPT_DIR=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
else
    echo "BASH_SOURCE not set, falling back to $0"
    SCRIPT_DIR=$(dirname "$0")
fi

echo "Current OS Arch: $(arch)"

if [[ -z "$2" ]]; then
    arch=$(arch)
else
    arch="$2"
fi

echo "Start upload dep [${dep_name}] for Arch(${arch})."

deps_path=$(realpath "$SCRIPT_DIR/../../deps")
download_path=$(realpath "$SCRIPT_DIR/../../_deps")
file_name="${dep_name}_${arch}.tar.gz"

if ! command -v artget >/dev/null 2>&1; then
    echo "artget is not available."
    curl -o artget https://cmc-szver-artifactory.cmc.tools.huawei.com/artifactory/CMC-Release/artget/install/prod/Latest/linux/artget
    chmod +x artget
    sudo mv artget /usr/local/bin
    echo "artget installed into /usr/local/bin/."
fi

artget_cmd=$(which artget)

# Collecting Environment Information
collect_info() {
    local version_file="$1" # get the version param

    echo "System information" >"${version_file}"
    {
        echo "============================="
        [ -n "${env_type}" ] && echo "Build Image: ${env_type}"
        echo "Operating System: $(uname -s)"             # Get the operating system name
        echo "Kernel Version: $(uname -r)"               # Obtaining the Kernel Version
        echo "Architecture: $(uname -m)"                 # Obtaining the System Architecture
        echo "GCC Version: $(gcc --version | head -n 1)" # Obtaining the GCC Version
        echo ""
    } >>"${version_file}"

    if [ "$(ps -p 1 -o comm=)" = "systemd" ] && command -v hostnamectl &>/dev/null; then
        {
            echo "Hostname Information:"
            echo "============================="
            hostnamectl
            echo ""
        } >>"${version_file}"
    fi

    {
        echo "Git information"
        echo "============================="
        echo "Git User Name: $(git config user.name)" # Obtaining the Git User Name
    } >>"${version_file}" # Get Git User Email

    echo "Information collected in ${version_file}."
}

# Check if the file exists
if [ ! -d "$deps_path/${dep_name}" ]; then
    echo "Dep [${dep_name}] does not exist. Please build first."
else
    echo "Dep [${dep_name}] already exists. Start upload."
    mkdir -p "${download_path}"

    version_file="$deps_path/${dep_name}/.version"
    collect_info "${version_file}"
    cd "${deps_path}" || exit
    tar -czvf "${download_path}/${file_name}" "$dep_name"
    echo "dep [${dep_name}] tar file has created in ${download_path}/${file_name}."
    ${artget_cmd} push "BeiMing 24.4.RC1.B099" \
        -ru software \
        -user p_OckCI \
        -pwd encryption:ETMsDgAAAYA7af+XABRBRVMvQ0JDL1BLQ1M1UGFkZGluZwCAABAAEJBiK1TiSbra9ngl+DLBe0QAAAAgYTU2Tt2GdcOS8nyU05sj8U0irLfK3CfgU6f4/7n6ohYAFJHxhHudG1F2Sf+r08BDVbQL9Bld \
        -ap "${download_path}/${file_name}"
    cd - || exit
    rm -f "${version_file}"
    [ -d "${download_path}" ] && rm -rf "${download_path}"
fi
