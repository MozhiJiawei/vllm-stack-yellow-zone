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
#include "test_arg_parse.h"

#include <fstream>
#include <string>
#include <vector>

#include "mockcpp/mokc.h"

#include "cmd.h"
#include "vas_cli_reg_builder.h"
#include "vas_security_manager.h"
#include "vasd_arg_parse.h"

using namespace vas::sched;

namespace vas::ut::arg {
static const uint16_t DEFAULT_THRESH = 85;

void TestArgParse::SetUp()
{
    testing::Test::SetUp();
    VasdArgParse::smt = true;
    VasdArgParse::schedPolicy = "affinity";
    VasdArgParse::dynamicAffinityUtilThresh = DEFAULT_THRESH;
    VasdArgParse::skippedCPUSet = "";
    VasdArgParse::rangeAffinity = true;
}

void TestArgParse::TearDown()
{
    testing::Test::TearDown();
    VasdArgParse::smt = true;
    VasdArgParse::schedPolicy = "affinity";
    VasdArgParse::dynamicAffinityUtilThresh = DEFAULT_THRESH;
    VasdArgParse::skippedCPUSet = "";
    VasdArgParse::rangeAffinity = true;
}

TEST_F(TestArgParse, testArgsInitialization)
{
    VasdArgParse::smt = false;
    VasdArgParse::schedPolicy = "dynamicAffinity";
    VasdArgParse::dynamicAffinityUtilThresh = 90;
    VasdArgParse::skippedCPUSet = "0-1";
    VasdArgParse::rangeAffinity = false;

    EXPECT_FALSE(VasdArgParse::smt);
    EXPECT_EQ(VasdArgParse::schedPolicy, "dynamicAffinity");
    EXPECT_EQ(VasdArgParse::dynamicAffinityUtilThresh, 90);
    EXPECT_EQ(VasdArgParse::skippedCPUSet, "0-1");
    EXPECT_FALSE(VasdArgParse::rangeAffinity);
}

TEST_F(TestArgParse, testArgsDeInitFunction)
{
    VasdArgParse::schedPolicy = "dynamicAffinity";
    EXPECT_EQ(VasdArgParse::DeInit(), VAS_OK);
    VasdArgParse::schedPolicy = "ErrorStratage";
    EXPECT_EQ(VasdArgParse::DeInit(), VAS_ERROR);
    VasdArgParse::schedPolicy = "affinity";
    EXPECT_EQ(VasdArgParse::DeInit(), VAS_OK);
}

TEST_F(TestArgParse, testArgsIsDynamicAffinityAvailable)
{
    std::ofstream file(PROC_CMDLINE);
    file << "dynamic_affinity=enable";
    file.close();
    EXPECT_FALSE(VasdArgParse::IsDynamicAffinityAvailable());
    std::remove(PROC_CMDLINE.string().c_str());
}

TEST_F(TestArgParse, testArgsInvalidDynamicAffinityUtilThresh)
{
    MOCKER(vas::security::VasSecurityManager::ModifyEffectiveCapabilities).stubs().will(returnValue(VAS_OK));
    VasRet ret = VasdArgParse::WriteDynamicAffinityUtilThresh(101);
    EXPECT_EQ(ret, VAS_ERROR);

    ret = VasdArgParse::WriteDynamicAffinityUtilThresh(-1);
    EXPECT_EQ(ret, VAS_ERROR);
}

TEST_F(TestArgParse, testArgsRegisterServerModuleSDK)
{
    vas::cli::reg::RegisterServerModuleSDK();
}

TEST_F(TestArgParse, testCliSetServerConfNormal)
{
    std::map<std::string, std::string> params = {{"smt", "true"},
                                                 {"sched-policy", "dynamicAffinity"},
                                                 {"dynamic-util-thresh", "85"},
                                                 {"skip-cpuset", "0-1"},
                                                 {"range-affinity", "false"}};

    cli::framework::VasCliSdkResult result = cli::reg::CliSetServerConfFunc(params);

    EXPECT_TRUE(VasdArgParse::smt);
    EXPECT_EQ(VasdArgParse::schedPolicy, "dynamicAffinity");
    EXPECT_EQ(VasdArgParse::dynamicAffinityUtilThresh, 85);
    EXPECT_EQ(VasdArgParse::skippedCPUSet, "0-1");
    EXPECT_FALSE(VasdArgParse::rangeAffinity);
}
} // namespace vas::ut::arg