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
googletest_dir_path=$1
out_path=$2
outfile_path="${out_path}/googletest"
current_dir=$(cd $(dirname "$0"); pwd)

[ -d "${outfile_path}" ] && rm -rf "${outfile_path}"
mkdir -p "${outfile_path}"

echo "[ begin to make ]"
cd "${googletest_dir_path}" || exit 1
cmake . -D CMAKE_PROJECT_INCLUDE=${current_dir}/include.cmake
make
if [ $? = 0 ];then
    cp -rf "${googletest_dir_path}"/lib "${outfile_path}"
    cp -rf "${googletest_dir_path}"/googletest/include "${outfile_path}"
    cp -rf "${googletest_dir_path}"/googlemock/include "${outfile_path}"
    echo "googletest build successfully"
    exit 0
else
    echo "googletest build failed"
    exit 1
fi
