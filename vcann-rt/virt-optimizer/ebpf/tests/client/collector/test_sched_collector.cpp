/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "client/collector/collector.h"
#include "client/collector/sched_collector.h"
#include "common/data_struct.h"

TEST(SchedCollectorTest, GetInstanceTest)
{
    SchedCollector &obj1 = SchedCollector::getInstance();
    SchedCollector &obj2 = SchedCollector::getInstance();
    EXPECT_EQ(&obj1, &obj2);
}