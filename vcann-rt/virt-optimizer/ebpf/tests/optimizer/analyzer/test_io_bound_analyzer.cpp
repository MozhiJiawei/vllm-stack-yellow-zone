/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "optimizer/analyzer/io_bound_analyzer.h"

TEST(IOBoundAnalyzerTest, IOBoundAnalyzerCase1)
{
    IOBoundAnalyzer io_analyzer;
    std::vector<std::string> data;
    data.push_back("123");
    std::vector<std::shared_ptr<BaseTuner>> res = io_analyzer.analyze(data);
    EXPECT_EQ(res.size(), 2);
}
