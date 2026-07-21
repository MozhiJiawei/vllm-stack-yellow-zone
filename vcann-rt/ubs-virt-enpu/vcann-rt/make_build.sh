#!/bin/bash
set -e
PROJECT_NAME=vcann-runtime
VERSION=1.0

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)

if [ -z "$ASCEND_HOME_PATH" ]; then
    echo "[ERROR] ASCEND_HOME_PATH is not set!"
    exit 1
fi

if [ -z "$ENPU_ASCEND_DRIVER_PATH" ]; then
    export ENPU_ASCEND_DRIVER_PATH="/usr/local/Ascend"
    echo "[WARNING] ENPU_ASCEND_DRIVER_PATH is not set, using default: $ENPU_ASCEND_DRIVER_PATH"
fi

BUILD_PATH="$CURRENT_PATH/build"
mkdir -p "$BUILD_PATH"
cd "$BUILD_PATH"

cmake_args=()
if [[ ${ENABLE_DEADLOCK_DIAGNOSTICS:-0} == 1 ]]; then
    cmake_args+=("-DENABLE_DEADLOCK_DIAGNOSTICS=ON")
    echo "[INFO] Building diagnostic libvruntime.so with -O2 and debugger symbols."
else
    cmake_args+=("-DENABLE_DEADLOCK_DIAGNOSTICS=OFF")
fi

if ! cmake .. "${cmake_args[@]}"; then
    echo "[ERROR] make_build:cmake failed.!"
    exit 1
fi

if ! make -j "$(nproc)"; then
    echo "[ERROR] make_build:make failed."
    exit 1
fi
