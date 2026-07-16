#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# Description: run ut script

set -e

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)

export BUILD_PATH="${CURRENT_PATH}/../build/debug/"

sudo mkdir -p /var/ubs-opt/data/ && sudo chmod 733 /var/ubs-opt/data/

sudo mkdir /usr/local/sbin/ubs-optimizer/ && sudo chmod 733 /usr/local/sbin/ubs-optimizer/

sudo touch /var/ubs-opt/data/data.json && sudo chmod 666 /var/ubs-opt/data/data.json

sudo touch /usr/local/sbin/ubs-optimizer/config.json && sudo chmod 666 /usr/local/sbin/ubs-optimizer/config.json

cd "${CURRENT_PATH}"/..

sh build.sh -t debug

cd "${BUILD_PATH}"/tests

./client/test-client
./common/test-common
./server/test-server
./optimizer/test-optimizer --gtest_output=xml:test_detail.xml

lcov --rc lcov_branch_coverage=1 -b ./src/ -d ${BUILD_PATH}/src/ -c -o lcov.info
lcov --rc lcov_branch_coverage=1 -r lcov.info '/usr/include/*' '*.h' '*main_server.cpp' '*main.cpp' -o coverage.info
lcov --rc lcov_branch_coverage=1 -e coverage.info '*common/*' '*collector/*' '*/optimizer/*' '*/server/control/*' -o coverage.info

genhtml --branch-coverage --rc lcov_branch_coverage=1 -o gcover_report coverage.info

cp -rf coverage.info gcover_report/

cp -rf test_detail.xml gcover_report/

sudo rm -rf /var/ubs-opt/

sudo rm -rf /usr/local/sbin/ubs-optimizer/