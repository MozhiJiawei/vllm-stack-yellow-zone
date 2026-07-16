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
#include "test_cpu_helper.h"

#include "cpu_helper.h"
#include "vasd_arg_parse.h"

namespace vas::ut::acquire {
using namespace vas::sched::acquire;
using namespace vas::sched;
void TestCpuHelper::SetUp()
{
    Test::SetUp();
}

void TestCpuHelper::TearDown()
{
    GlobalMockObject::verify();
    Test::TearDown();
}

TEST_F(TestCpuHelper, InitCpuHelperSuccess)
{
    EXPECT_EQ(CpuHelper::Init(), VAS_OK);
}

TEST_F(TestCpuHelper, GetCpuTopologyTest)
{
    CpuHelper cpuHelper;
    CpuTopologyMap topology = cpuHelper.GenCpuTopology();
    EXPECT_FALSE(topology.empty());
    EXPECT_FALSE(cpuHelper.GetCpu2NumaIdMap().empty());
    EXPECT_FALSE(cpuHelper.GetNuma2CpusetMap().empty());
    EXPECT_EQ(cpuHelper.GetSmtCpuNr(), CpuHelper::GetThreadSiblings(0));
    VasdArgParse::skippedCPUSet = "0";
    topology = cpuHelper.GenCpuTopology();
    EXPECT_TRUE(topology.begin()->second.bitMap[0]);
    VasdArgParse::skippedCPUSet = "";
    EXPECT_EQ(cpuHelper.GetCpuTopology(), topology);
}
} // namespace vas::ut::acquire