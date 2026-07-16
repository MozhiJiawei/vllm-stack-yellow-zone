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
#include "test_vas_cli_res_echo.h"

#include "vas_cli_res_echo.h"

using namespace vas::cli::framework;
namespace vas::ut::cli {
using namespace vas::cli::framework;
void TestVasCliResEcho::SetUp()
{
    Test::SetUp();
}

void TestVasCliResEcho::TearDown()
{
    Test::TearDown();
}

TEST_F(TestVasCliResEcho, IndividualWordsDisplay)
{
    VasCliResEcho echoCtl;
    VasRet result = echoCtl.IndividualWordsDisplay("this is a test");
    EXPECT_EQ(result, 0);
}

TEST_F(TestVasCliResEcho, NormalStringPrintDisplay)
{
    VasCliResEcho echoCtl;
    std::map<int, std::string> remainingCellData;
    int columnWidth = 10;
    echoCtl.StringPrintDisplay("This is a very long text that will be wrapped to multiple lines");
    EXPECT_TRUE(remainingCellData.empty());
}
} // namespace vas::ut::cli