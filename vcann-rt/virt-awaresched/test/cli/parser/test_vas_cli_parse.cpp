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
#include "test_vas_cli_parse.h"

#include <string>
#include <vector>

#include "vas_cli_parse.h"
#include "vas_cli_reg_builder.h"

using namespace vas::cli::framework;

namespace vas::ut::cli {
inline constexpr int NUMBER_ZERO = 0;
inline constexpr int NUMBER_ONE = 1;
inline constexpr int NUMBER_TWO = 2;
inline constexpr int BUFFER_SIZE = 1024;
void TestVasCliParse::SetUp()
{
    Test::SetUp();
    VasCliParse::GetInstance().Reset();
}

void TestVasCliParse::TearDown()
{
    Test::TearDown();
    VasCliParse::GetInstance().Reset();
}

TEST_F(TestVasCliParse, testCliRegisterSdkCmdInfoNormal)
{
    std::vector<VasCliSdkOptionsInfo> params;
    int infoSize = 3;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    sdkRegInfo.push_back({"command2", "type2", "", {}, nullptr});
    sdkRegInfo.push_back({"command3", "type3", "", {}, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_EQ(info.size(), infoSize);
    EXPECT_STREQ(info[NUMBER_ZERO].command.c_str(), "command1");
    EXPECT_STREQ(info[NUMBER_ONE].command.c_str(), "command2");
    EXPECT_STREQ(info[NUMBER_TWO].command.c_str(), "command3");
    EXPECT_STREQ(info[NUMBER_ZERO].type.c_str(), "type1");
    EXPECT_STREQ(info[NUMBER_ONE].type.c_str(), "type2");
    EXPECT_STREQ(info[NUMBER_TWO].type.c_str(), "type3");
}

TEST_F(TestVasCliParse, testCliRegisterSdkCmdInfoInputNull)
{
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_EQ(info.size(), VAS_OK);
}

TEST_F(TestVasCliParse, testCliRegisterSdkCmdInfoCmdNull)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkOptionsInfo> params1;
    params1.push_back({"", "test1", "this is option test1"});
    std::vector<VasCliSdkOptionsInfo> params2;
    params2.push_back({"t1", "", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", {}, nullptr});
    sdkRegInfo.push_back({"command2", "type2", "", params, nullptr});
    sdkRegInfo.push_back({"command3", "type3", "", params1, nullptr});
    sdkRegInfo.push_back({"command4", "type4", "", params2, nullptr});
    sdkRegInfo.push_back({"", "type5", "", params, nullptr});
    sdkRegInfo.push_back({"command6", "", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    int infoSize = 2;
    EXPECT_EQ(info.size(), infoSize);
    EXPECT_STREQ(info[NUMBER_ZERO].command.c_str(), "command1");
    EXPECT_STREQ(info[NUMBER_ONE].command.c_str(), "command2");
    EXPECT_STREQ(info[NUMBER_ZERO].type.c_str(), "type1");
    EXPECT_STREQ(info[NUMBER_ONE].type.c_str(), "type2");
}

TEST_F(TestVasCliParse, testCliRegisterSdkCmdInfoModuleNull)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_EQ(info.size(), VAS_ERROR);
}

TEST_F(TestVasCliParse, testSetSdkCmdInfoWithOptsMapNormal)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    params.push_back({"t2", "test2", "this is option test2"});
    params.push_back({"t3", "test3", "this is option test3"});
    VasCliSdkCmdInfo sdkRegInfo{"command1", "type1", "", params, nullptr};

    VasCliParse::GetInstance().SetSdkCmdInfoWithOptsMap(sdkRegInfo);
    std::string command = "command1";
    std::string type = "type1";
    std::string key = command + "_" + type;
    auto opts = VasCliParse::GetInstance().GetSdkCommandWithOptions().find(key);
    EXPECT_STREQ(opts->second[NUMBER_ZERO].longOpts.c_str(), "test1");
    EXPECT_STREQ(opts->second[NUMBER_ZERO].shortOpts.c_str(), "t1");
}

TEST_F(TestVasCliParse, testSetSdkCmdInfoWithOptsMapCommandLenLong)
{
    std::string command = "command1xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    std::string type = "type1";
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({command, type, "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_TRUE(info.empty());
}

TEST_F(TestVasCliParse, testSetSdkCmdInfoWithOptsMapTypeLenLong)
{
    std::string command = "command1";
    std::string type = "type1xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({command, type, "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_TRUE(info.empty());
}

TEST_F(TestVasCliParse, testSetSdkCmdInfoWithOptsMapNameLenLong)
{
    std::string command = "command1";
    std::string type = "type1";
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back(
        {"t1", "longnamexxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({command, type, "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    auto info = VasCliParse::GetInstance().GetSdkCmdInfo();
    EXPECT_TRUE(info.empty());
}

TEST_F(TestVasCliParse, testGetSdkCommandInfoNoExit)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    sdkRegInfo.push_back({"command2", "type2", "", {}, nullptr});
    sdkRegInfo.push_back({"command3", "type3", "", {}, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command4", "type1"};
    VasCliParse::GetInstance().SdkCliParse(args);
    auto info = VasCliParse::GetInstance().GetSdkCommandInfo();
    EXPECT_EQ(info.command, "");
    EXPECT_EQ(info.type, "");
}

TEST_F(TestVasCliParse, testParseOneCommandPrtHelpInfo)
{
    std::vector<VasCliSdkOptionsInfo> sdkParams;
    sdkParams.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", sdkParams, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    ASSERT_NO_THROW(VasCliParse::GetInstance().ParseOneCommandPrtHelpInfo("command1", "type1"));
}

TEST_F(TestVasCliParse, testCommandTypeParamsHelpInfo)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    params.push_back({"t2", "test2", "this is option test2"});
    ASSERT_NO_THROW(VasCliParse::GetInstance().CommandTypeParamsHelpInfo(params, NUMBER_ONE));
    ASSERT_NO_THROW(VasCliParse::GetInstance().CommandTypeParamsHelpInfo(params, BUFFER_SIZE));
}

TEST_F(TestVasCliParse, testGetInputOptionMapNormal)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    sdkRegInfo.push_back({"command2", "type2", "", {}, nullptr});
    sdkRegInfo.push_back({"command3", "type3", "", {}, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "--test1", "aaa"};
    VasCliParse::GetInstance().SdkCliParse(args);
    auto info = VasCliParse::GetInstance().GetSdkCommandInfo();
    EXPECT_EQ(info.command, "command1");
    EXPECT_EQ(info.type, "type1");
    auto infoMap = VasCliParse::GetInstance().GetInputOptionMap();
    EXPECT_EQ(infoMap.find("test1")->second, "aaa");
}

TEST_F(TestVasCliParse, testGetInputOptionMapNoExit)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command4", "type1", "-t1", "aaa"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

TEST_F(TestVasCliParse, testInputDupOpt)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "-t1", "aaa", "--test1", "aaa"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

TEST_F(TestVasCliParse, testInputOptNoParam)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "-t1"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

TEST_F(TestVasCliParse, testInputUnknownOpt)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "-t2", "aaa"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

TEST_F(TestVasCliParse, testGetInputOptionMapNoOptExit)
{
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", {}, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "-t1", "aaa"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

TEST_F(TestVasCliParse, testGetInputOptionMapNoOpt)
{
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", {}, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1"};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_OK);
}

TEST_F(TestVasCliParse, testInputTooLongValue)
{
    std::vector<VasCliSdkOptionsInfo> params;
    params.push_back({"t1", "test1", "this is option test1"});
    std::vector<VasCliSdkCmdInfo> sdkRegInfo;
    sdkRegInfo.push_back({"command1", "type1", "", params, nullptr});
    VasCliParse::VasCliRegisterSdkCmdInfo(sdkRegInfo);
    std::vector<std::string> args{"vasctl", "command1", "type1", "-t1", std::string("a", 1025)};
    auto res = VasCliParse::GetInstance().SdkCliParse(args);
    EXPECT_EQ(res, VAS_ERROR);
}

} // namespace vas::ut::cli