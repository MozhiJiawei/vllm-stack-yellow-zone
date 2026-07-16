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

BUILD_FOLDER=$1
CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)

cd "${CURRENT_PATH}"
mkdir -p "${CURRENT_PATH}"/rpm/usr/local/sbin/ubs-optimizer

cp -f "${CURRENT_PATH}"/build/"${BUILD_FOLDER}"/src/client/ubs-opt "${CURRENT_PATH}"/rpm/usr/local/sbin
cp -f "${CURRENT_PATH}"/build/"${BUILD_FOLDER}"/src/server/ubs-opt-guard "${CURRENT_PATH}"/rpm/usr/local/sbin
cp -f "${CURRENT_PATH}"/build/"${BUILD_FOLDER}"/src/optimizer/ubs-opt-tuner "${CURRENT_PATH}"/rpm/usr/local/sbin
cp -f "${CURRENT_PATH}"/ebpf/src/default_config.json "${CURRENT_PATH}"/rpm/usr/local/sbin/ubs-optimizer/config.json

/usr/bin/tar -zcf ubs_optimizer.tar.gz ./rpm/
rm -rf "${CURRENT_PATH}"/rpm

mkdir -p ~/rpmbuild/SOURCES/
cp ubs_optimizer.tar.gz ~/rpmbuild/SOURCES/
rm -f ubs_optimizer.tar.gz

rpmbuild --define "package_name ubs_optimizer" -bb ubs_optimizer.spec

mkdir -p "${CURRENT_PATH}/output/${BUILD_FOLDER}"
cp ~/rpmbuild/RPMS/"$(uname -m)"/ubs-optimizer-0.1.0-k5.1.*.rpm "${CURRENT_PATH}"/output/"${BUILD_FOLDER}"/ubs-optimizer-0.1.0-k5.1-"$(uname -m)".rpm