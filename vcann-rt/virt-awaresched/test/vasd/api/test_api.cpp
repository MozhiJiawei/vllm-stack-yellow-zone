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

#include "test_api.h"

#include <map>
#include <string>

#include "mockcpp/mokc.h"

#include "api.h"
#include "cmd.h"
#include "cmd_serialize.h"
#include "vasd_arg_parse.h"

using namespace vas::sched;
using namespace vas::sched::ut;

namespace vas::sched::ut {
void TestApi::SetUp()
{
    testing::Test::SetUp();
    Api::cmdMap.clear();
}

void TestApi::TearDown()
{
    testing::Test::TearDown();
    Api::cmdMap.clear();
}

TEST_F(TestApi, testSocketMsgHandlerUnknownCommand)
{
    std::string cmd = "unknown_command";
    std::string resStr;
    VasRet ret = Api::SocketMsgHandler(cmd, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_NE(resStr.find("Command option not found"), std::string::npos);
}

TEST_F(TestApi, testSetConfigEmptyParams)
{
    std::map<std::string, std::string> data;
    std::string resStr;
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "SetConfig failed, params is empty.");
}

TEST_F(TestApi, testSetConfigInvalidParams)
{
    std::map<std::string, std::string> data;
    data["invalid_key"] = "test_value";
    std::string resStr;
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "SetConfig failed, Unknown params.");
}

TEST_F(TestApi, testSetConfigEqualParams)
{
    VasdArgParse::schedPolicy = "affinity";
    std::map<std::string, std::string> data;
    data[SCHED_POLICY] = "affinity";
    std::string resStr;
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_OK);
    EXPECT_EQ(resStr, "SetConfig success.");
}

TEST_F(TestApi, testSetConfigFailDeInitAndInit)
{
    VasdArgParse::schedPolicy = "affinity";
    std::map<std::string, std::string> data;
    data[SCHED_POLICY] = "dynamicAffinity";
    std::string resStr;
    MOCKER(VasdArgParse::DeInit).stubs().will(returnValue(VAS_ERROR));
    MOCKER(VasdArgParse::Init).stubs().will(returnValue(VAS_ERROR));
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "SetConfig failed, Failed to set the new configuration. Rollback to original configuration");
    MOCKER(VasdArgParse::DeInit).reset();
    MOCKER(VasdArgParse::Init).reset();
}

TEST_F(TestApi, testQueryCpuAffinityInfoNormal)
{
    std::map<std::string, std::string> data;
    data["scope"] = "test_scope";
    std::string resStr;
    VasRet ret = Api::QueryCpuAffinityInfo(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "No affinity information available");
}

TEST_F(TestApi, testQueryCpuAffinityInfoEmptyParams)
{
    std::map<std::string, std::string> data;
    std::string resStr;
    VasRet ret = Api::QueryCpuAffinityInfo(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "QueryCpuAffinityInfo failed, params is empty.");
}

TEST_F(TestApi, testQueryCpuAffinityInfoInvalidParams)
{
    std::map<std::string, std::string> data;
    data["invalid_key"] = "test_value";
    std::string resStr;
    VasRet ret = Api::QueryCpuAffinityInfo(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "QueryCpuAffinityInfo failed, unknown params.");
}

TEST_F(TestApi, testReAssignCpuNotExist)
{
    std::map<std::string, std::string> data;
    data["scope"] = "test_scope";
    std::string resStr;
    VasRet ret = Api::ReAssign(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "Virtual machine needs to reassign cpu does not exist");
}

TEST_F(TestApi, testReAssignEmptyParams)
{
    std::map<std::string, std::string> data;
    std::string resStr;
    VasRet ret = Api::ReAssign(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "ReAssign failed, params is empty.");
}

TEST_F(TestApi, testReAssignInvalidParams)
{
    std::map<std::string, std::string> data;
    data["invalid_key"] = "test_value";
    std::string resStr;
    VasRet ret = Api::ReAssign(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "ReAssign failed, unknown params.");
}

TEST_F(TestApi, testSocketMsgHandlerEmptyCommand)
{
    std::string cmd = "";
    std::string resStr;
    VasRet ret = Api::SocketMsgHandler(cmd, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_NE(resStr.find("Failed to deserialize command."), std::string::npos);
}

TEST_F(TestApi, testSocketMsgHandlerInvalidCommandFormat)
{
    std::string cmd = "invalid_command_format";
    std::string resStr;
    VasRet ret = Api::SocketMsgHandler(cmd, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_NE(resStr.find("Command option not found"), std::string::npos);
}

TEST_F(TestApi, testSetConfigMultipleParams)
{
    std::map<std::string, std::string> data;
    data["sched-policy"] = "test_policy";
    data["invalid_key"] = "test_value";
    std::string resStr;
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "Invalid params, please try with dynamicAffinity or affinity");
}

TEST_F(TestApi, testSetConfigEmptyValue)
{
    std::map<std::string, std::string> data;
    data["sched-policy"] = "";
    std::string resStr;
    VasRet ret = Api::SetConfig(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "Invalid params, please try with dynamicAffinity or affinity");
}

TEST_F(TestApi, testQueryCpuAffinityInfoInvalidScope)
{
    std::map<std::string, std::string> data;
    data["scope"] = "invalid_scope";
    std::string resStr;
    VasRet ret = Api::QueryCpuAffinityInfo(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "No affinity information available");
}

TEST_F(TestApi, testQueryCpuAffinityInfoEmptyResult)
{
    std::map<std::string, std::string> data;
    data["scope"] = "test_scope";
    std::string resStr;
    VasRet ret = Api::QueryCpuAffinityInfo(data, resStr);
    EXPECT_EQ(ret, VAS_ERROR);
    EXPECT_EQ(resStr, "No affinity information available");
}
} // namespace vas::sched::ut