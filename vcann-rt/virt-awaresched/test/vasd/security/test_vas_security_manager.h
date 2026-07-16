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

#ifndef TEST_VAS_SECURITY_MANAGER_H
#define TEST_VAS_SECURITY_MANAGER_H

#include <gtest/gtest.h>

namespace vas::ut::security {
class TestVasSecurityManager : public testing::Test {
public:
    TestVasSecurityManager() = default;

private:
    void SetUp() override;

    void TearDown() override;
};
} // namespace vas::ut::security
#endif // TEST_VAS_SECURITY_MANAGER_H
