/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef VIRTAWARESCHED_TEST_STRING_UTIL_H
#define VIRTAWARESCHED_TEST_STRING_UTIL_H

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "string_util.h"

namespace vas::ut::util {
class TestStringUtil : public testing::Test {
protected:
    TestStringUtil() = default;

private:
    void SetUp() override {}

    void TearDown() override {}
};
} // namespace vas::ut::util

#endif // VIRTAWARESCHED_TEST_STRING_UTIL_H
