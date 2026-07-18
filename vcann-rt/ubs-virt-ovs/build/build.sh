#!/usr/bin/env bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# ubs-virt-ovs is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
set -e

PROJECT_NAME=ubs-virt-ovs
VERSION=1.0.0
RELEASE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

# ============ paths ============
RPMBUILD_DIR="${ROOT_DIR}/build/rpmbuild"
OUTPUT_DIR="${ROOT_DIR}/build/output"
BUILDUT_DIR="${ROOT_DIR}/buildUT"
DEPS_DIR="${ROOT_DIR}/deps"
GCOVER_DIR="${ROOT_DIR}/gcover_report"

# ============ build type ============
BUILD_TYPE=${1:-relwithdebinfo}
BUILD_TYPE_LOWER=$(echo "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')

case "${BUILD_TYPE_LOWER}" in
  debug)
    CMAKE_BUILD_TYPE=Debug
    RPM_RELEASE_SUFFIX=.debug
    ;;
  release)
    CMAKE_BUILD_TYPE=Release
    RPM_RELEASE_SUFFIX=
    ;;
  relwithdebinfo)
    CMAKE_BUILD_TYPE=RelwithDebinfo
    RPM_RELEASE_SUFFIX=.rel
    ;;
  clean)
    echo "[INFO] Cleaning build artifacts..."
    rm -rf "${RPMBUILD_DIR}"
    rm -rf "${OUTPUT_DIR}"
    rm -rf "${BUILDUT_DIR}"
    rm -rf "${DEPS_DIR}"
    rm -rf "${GCOVER_DIR}"
    rm -rf "${SCRIPT_DIR}/CMakeCache.txt"
    rm -rf "${SCRIPT_DIR}/CMakeFiles"
    rm -rf "${SCRIPT_DIR}/Makefile"
    rm -rf "${SCRIPT_DIR}/cmake_install.cmake"
    rm -rf "${SCRIPT_DIR}/compile_commands.json"
    rm -rf "${SCRIPT_DIR}/src"
    echo "[INFO] Clean done."
    exit 0
    ;;
  dt)
    cd "${ROOT_DIR}"
    bash "${SCRIPT_DIR}/run_ut.sh"
    exit 0
    ;;
  *)
    echo "Usage: $0 [debug|release|relwithdebinfo|clean]"
    exit 1
    ;;
esac

SPEC_FILE="${SCRIPT_DIR}/${PROJECT_NAME}.spec"

[[ -f "${SPEC_FILE}" ]] || {
  echo "[ERROR] spec file not found: ${SPEC_FILE}"
  exit 1
}

[[ -f "${ROOT_DIR}/CMakeLists.txt" ]] || {
  echo "[ERROR] not a valid project root: ${ROOT_DIR}"
  exit 1
}

echo "[INFO] Build type: ${CMAKE_BUILD_TYPE}"
echo "[INFO] Project root: ${ROOT_DIR}"
echo "[INFO] Spec file: ${SPEC_FILE}"

rm -rf "${RPMBUILD_DIR}"
mkdir -p "${RPMBUILD_DIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS} "${OUTPUT_DIR}"

# ============ create source tar ============
SRC_TAR="${RPMBUILD_DIR}/SOURCES/${PROJECT_NAME}-${VERSION}.tar.gz"
cp "${ROOT_DIR}/../LICENSE" "${ROOT_DIR}/LICENSE"

# Go to project root
cd "${ROOT_DIR}"/..
tar czf "${SRC_TAR}" \
  --exclude=build/rpmbuild \
  --exclude=build/output \
  --exclude=.git \
  --transform "s,^ubs-virt-ovs,${PROJECT_NAME}-${VERSION}," \
  ubs-virt-ovs

rm "${ROOT_DIR}/LICENSE"

echo "[INFO] Source tar created: ${SRC_TAR}"

# ============ copy spec file ============
cp "${SPEC_FILE}" "${RPMBUILD_DIR}/SPECS/${PROJECT_NAME}.spec"

# ============ cross compile options ============
# Set the cross-compilation prefix (can be adjusted according to the target platform)
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}

# ============ build RPM ============
rpmbuild \
  --define "_topdir ${RPMBUILD_DIR}" \
  --define "version ${VERSION}" \
  --define "release ${RELEASE}${RPM_RELEASE_SUFFIX}" \
  --define "cmake_build_type ${CMAKE_BUILD_TYPE}" \
  --define "cross_compile_prefix ${CROSS_COMPILE}" \
  -ba "${RPMBUILD_DIR}/SPECS/${PROJECT_NAME}.spec"

# ============ collect built RPMs ============
find "${RPMBUILD_DIR}/RPMS" -name "*.rpm" -exec cp -v {} "${OUTPUT_DIR}" \;

echo "[INFO] Output RPMs:"
ls -lh "${OUTPUT_DIR}"