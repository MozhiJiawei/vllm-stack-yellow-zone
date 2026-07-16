/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "client/collector/collector.h"
#include "client/collector/ipi_collector.h"
#include "common/data_struct.h"

TEST(IPICollectorTest, GetInstanceTest)
{
    IPICollector &obj1 = IPICollector::getInstance();
    IPICollector &obj2 = IPICollector::getInstance();
    EXPECT_EQ(&obj1, &obj2);
}
