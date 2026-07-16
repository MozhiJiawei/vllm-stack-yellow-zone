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
#include "test_cmd_serialize.h"

#include "cmd_serialize.h"

using namespace vas::common;
namespace vas::ut::serialize {
void TestCmdSerialize::SetUp()
{
    Test::SetUp();
}

void TestCmdSerialize::TearDown()
{
    Test::TearDown();
}

TEST_F(TestCmdSerialize, serializeTest)
{
    CmdOption cmdOpt;
    cmdOpt.option = "test_option";
    cmdOpt.params = {{"key1", "value1"}, {"key2", "value2"}};

    std::string cmdStr;
    EXPECT_EQ(CmdSerialize::Serialize(cmdOpt, cmdStr), VAS_OK);

    // Verifying Serialization Results
    // Note: Since the order of parameters may be uncertain, we need to check flexibly.
    EXPECT_TRUE(cmdStr == "test_option;key1:value1;key2:value2" || cmdStr == "test_option;key2:value2;key1:value1");
}

// Testing basic deserialization scenarios
TEST_F(TestCmdSerialize, DeSerializeBasicTest)
{
    std::string cmdStr = "test_option;key1:value1;key2:value2";
    CmdOption cmdOpt;
    int paramSize = 2;
    EXPECT_EQ(CmdSerialize::DeSerialize(cmdStr, cmdOpt), VAS_OK);

    EXPECT_EQ(cmdOpt.option, "test_option");
    EXPECT_EQ(cmdOpt.params.size(), paramSize);
    EXPECT_EQ(cmdOpt.params["key1"], "value1");
    EXPECT_EQ(cmdOpt.params["key2"], "value2");
}
} // namespace vas::ut::serialize