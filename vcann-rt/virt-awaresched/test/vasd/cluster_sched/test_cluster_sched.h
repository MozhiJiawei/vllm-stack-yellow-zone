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

#ifndef TEST_CLUSTER_SCHED_H
#define TEST_CLUSTER_SCHED_H

#include <gtest/gtest.h>

#include "cluster_sched.h"
#include "cpu_helper.h"
#include "libvirt_helper.h"

namespace vas::ut::sched {
using namespace vas::common;
using namespace vas::sched::acquire;
using namespace vas::sched;

class TestClusterSched : public testing::Test {
public:
    static std::string uuid01;
    static VmInfoMap oneVmInfo;
    static NumaClusterMap defaultNumaClusterMap;
    static DomainMap defaultDomainMap;
    static EntityMap defaultEntityMap;
    static GroupMap defaultGroupMap;
    static uint16_t defaultCompactionCount;
    static uint8_t defaultOverProvision;
    static std::set<std::string> defaultVmNeedAssignTgIdCpu;
    static std::set<std::string> defaultVmNeedAssignIoThreadCpu;
    static CpuSet cluster0CpuList;
    static CpuTopologyMap defaultCpuTopologyMap;

protected:
    TestClusterSched() = default;

private:
    void SetUp() override;
    void TearDown() override;
};
} // namespace vas::ut::sched

#endif // TEST_CLUSTER_SCHED_H
