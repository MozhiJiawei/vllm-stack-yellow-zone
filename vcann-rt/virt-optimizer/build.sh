#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#
# ubs-optimizer is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set -e

BUILD_PATH=""
BUILD_TYPE=Release
BUILD_FOLDER=release
CLEAN=false
CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
while true; do
    case "$1" in
        -t | --type )
            type=$2
            type=${type,,}
            [[ "$type" != "debug" && "$type" != "release" ]] && echo "Invalid build type $2" && usage
            if [[ "$type" == "debug" ]]; then
              BUILD_TYPE=Debug
              BUILD_FOLDER=debug
              CMAKE_FLAGS+='-DBUILD_TEST=ON'
            elif [[ "$type" == "release" ]]; then
              BUILD_TYPE=Release
              BUILD_FOLDER=release
              CMAKE_FLAGS+='-DBUILD_TEST=OFF'
            fi
            shift 2
            ;;
        -c | --clean )
            CLEAN=true
            shift
            ;;
        -h | --help )
            usage
            exit 0
            ;;
        * )
            break;;
    esac
done

if [ -z "${BUILD_PATH}" ]; then
    BUILD_PATH=${CURRENT_PATH}/build/${BUILD_FOLDER}
fi

if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory"
    [ -n "${BUILD_PATH}" ] && rm -rf "${BUILD_PATH}"
fi

USR_VMLINUX_H_PATH="/usr/include/vmlinux.h"
VMLINUX_H_PATH="${CURRENT_PATH}"/ebpf/src/client/bpfs/vmlinux.h
if [[ ! -f "${VMLINUX_H_PATH}" && ! -f "${USR_VMLINUX_H_PATH}" ]]; then
    if ! sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > "${VMLINUX_H_PATH}"; then
        if [ -f "${VMLINUX_H_PATH}" ]; then
            rm -f "${VMLINUX_H_PATH}"
        fi
        echo "Failed to dump vmlinux.h"
        exit 1
    fi
    echo "vmlinux.h dumped successfully."
else
    echo "vmlinux.h already exists."
fi

cd "${CURRENT_PATH}/ebpf"

if ! cmake -S . -B "${BUILD_PATH}" -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
    echo "cmake configure failed."
    exit 1
fi
echo "cmake configure success."

if ! cmake --build "${BUILD_PATH}" --parallel; then
    echo "cmake build failed."
    exit 1
fi
echo "cmake build success."
cd "${CURRENT_PATH}"
bash "${CURRENT_PATH}/build_rpm.sh" "${BUILD_FOLDER}"


