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

downloadThirdDepFromCmc() {
    echo "Dep ${dep_name} does not exist. Downloading..."
    mkdir -p "$deps_path"
    artget pull "BeiMing 24.4.RC1.B099" \
      -ru software \
      -rp "${file_name}" \
      -user "p_OckCI" \
      -pwd "encryption:ETMsDgAAAYA7af+XABRBRVMvQ0JDL1BLQ1M1UGFkZGluZwCAABAAEJBiK1TiSbra9ngl+DLBe0QAAAAgYTU2Tt2GdcOS8nyU05sj8U0irLfK3CfgU6f4/7n6ohYAFJHxhHudG1F2Sf+r08BDVbQL9Bld" \
      -ap "${download_path}"
    if [ $? -eq 0 ]; then
        echo "File downloaded successfully."
    else
        echo "Failed to download file."
        exit 1
    fi
    echo "Unpacking ${file_name} to ${deps_path}"
    tar -xzvf "${download_path}/${file_name}" -C ${deps_path}
    echo "Unpacked ${file_name}."
    cd -
    rm -rf ${download_path}
}

# Check the number of parameters
if [ "$#" -ne 1 ]; then
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

arch=$(arch) # Acquire Architecture
deps_path=$(readlink -f "$SCRIPT_DIR/../deps")
download_path=$(readlink -f "$SCRIPT_DIR/../_deps")
file_name="${dep_name}_${arch}.tar.gz"

if ! command -v artget >/dev/null 2>&1; then
    echo "artget is not available."
    # download artget
    curl -o artget https://cmc-szver-artifactory.cmc.tools.huawei.com/artifactory/CMC-Release/artget/install/prod/Latest/linux/artget
    chmod +x artget
    sudo mv artget /usr/local/bin
    echo "artget installed into /usr/local/bin/."
fi

artget_cmd=$(which artget)

# Check if the file exists
if [ ! -d "$deps_path/${dep_name}" ]; then
    downloadThirdDepFromCmc
else
    echo "Dep ${dep_name} already exists."
fi
