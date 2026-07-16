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
#ifndef VIRT_AWARESCHED_TEST_VAS_CLI_RES_ECHO_H
#define VIRT_AWARESCHED_TEST_VAS_CLI_RES_ECHO_H

#include "gtest/gtest.h"

namespace vas::ut::cli {
class TestVasCliResEcho : public testing::Test {
protected:
    TestVasCliResEcho() = default;

private:
    void SetUp() override;

    void TearDown() override;
};
} // namespace vas::ut::cli
#endif // VIRT_AWARESCHED_TEST_VAS_CLI_RES_ECHO_H
