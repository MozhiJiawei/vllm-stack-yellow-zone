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
#include "test_proc_helper.h"

#include "libvirt_helper.h"
#include "proc_helper.h"

namespace vas::ut::acquire {
using namespace vas::sched::acquire;
static const std::string UUID = "3faa6728-6027-4bd1-a858-6107d2221428";
static const pid_t PID = 1;
void TestProcHelper::SetUp()
{
    Test::SetUp();
}

void TestProcHelper::TearDown()
{
    GlobalMockObject::verify();
    Test::TearDown();
}

TEST_F(TestProcHelper, GetVmProcListTest)
{
    std::vector<std::string> uuids;
    uuids.emplace_back(UUID);
    MOCKER(&ProcHelper::TraverseProcWithCond).stubs().will(returnValue(VAS_ERROR)).then(returnValue(VAS_OK));
    EXPECT_EQ(ProcHelper::GetInstance().GetVmProcList(uuids).size(), 0);

    ProcHelper procHelper;
    MOCKER(&ProcHelper::ResetProcInfo).stubs();
    MOCKER(ProcHelper::GetVcpuList).stubs().will(returnValue(VAS_ERROR));
    procHelper.uuid2PidMap[UUID] = PID;
    EXPECT_EQ(procHelper.GetVmProcList(uuids).size(), 0);
    MOCKER(ProcHelper::GetVcpuList).reset();

    MOCKER(ProcHelper::GetVcpuList).stubs().will(returnValue(VAS_OK));
    EXPECT_NO_THROW(procHelper.GetVmProcList(uuids));
    procHelper.uuid2PidMap.clear();
}

TEST_F(TestProcHelper, GetVcpuListTest)
{
    Vcpu2PidMap vcpu2PidMap{};
    ProcHelper::GetVcpuList(PID, vcpu2PidMap);
    ProcHelper::GetVcpuList(-1, vcpu2PidMap);
}
} // namespace vas::ut::acquire