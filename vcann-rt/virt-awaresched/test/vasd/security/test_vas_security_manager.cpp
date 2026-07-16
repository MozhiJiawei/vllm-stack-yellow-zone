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

#include "test_vas_security_manager.h"

#include <mockcpp/mockcpp.hpp>

#include "error.h"
#include "logger.h"
#include "vas_security_manager.h"

namespace vas::ut::security {
using namespace vas::common;
using namespace vas::security;
void TestVasSecurityManager::SetUp()
{
    Test::SetUp();
}

void TestVasSecurityManager::TearDown()
{
    Test::TearDown();
}

TEST_F(TestVasSecurityManager, testGetCapabilities)
{
    MOCKER(VasSecurityManager::GetCap).stubs().will(returnValue(-1)).then(returnValue(0));
    EXPECT_EQ(VasSecurityManager::GetCapabilities(), VAS_ERROR);
    EXPECT_EQ(VasSecurityManager::GetCapabilities(), VAS_OK);
}

TEST_F(TestVasSecurityManager, testSetInitialCapabilities)
{
    MOCKER(VasSecurityManager::SetCap).stubs().will(returnValue(-1)).then(returnValue(0));
    EXPECT_EQ(VasSecurityManager::SetInitialCapabilities(), VAS_ERROR);
    EXPECT_EQ(VasSecurityManager::SetInitialCapabilities(), VAS_OK);
}

TEST_F(TestVasSecurityManager, testModifyEffectiveCapabilities)
{
    const std::vector<__u32> caps = {
        CAP_FOWNER,
    };
    int effectiveCapabilities = 999;
    MOCKER(VasSecurityManager::GetCap).stubs().will(returnValue(0)).then(returnValue(0));
    MOCKER(VasSecurityManager::SetCap).stubs().will(returnValue(0)).then(returnValue(0));
    EXPECT_EQ(VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_ADD), VAS_OK);
    EXPECT_EQ(VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_DELETE), VAS_OK);
    EXPECT_EQ(
        VasSecurityManager::ModifyEffectiveCapabilities(caps, static_cast<VasCapOperateType>(effectiveCapabilities)),
        VAS_ERROR_INVAL);
}

TEST_F(TestVasSecurityManager, testClearCapabilities)
{
    const std::vector<__u32> caps = {
        CAP_DAC_OVERRIDE,
    };
    MOCKER(VasSecurityManager::GetCap).stubs().will(returnValue(0)).then(returnValue(-1));
    MOCKER(VasSecurityManager::SetCap).stubs().will(returnValue(0)).then(returnValue(-1));
    EXPECT_EQ(VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_ADD), VAS_OK);
    VasSecurityManager::ClearCapabilities(caps);
}

} // namespace vas::ut::security