/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "optimizer/analyzer/irq_anomaly_analyzer.h"
#include "optimizer/tuner/gic_tuner.h"
#include "optimizer/tuner/halt_poll_tuner.h"

using namespace std::string_literals;

TEST(IRQAnomalyAnalyzerTest, Constructor)
{
    IRQAnomalyAnalyzer analyzer;

    EXPECT_EQ(analyzer.tuners_.size(), 2);

    EXPECT_NE(dynamic_cast<HaltPollTuner *>(analyzer.tuners_[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<GICTuner *>(analyzer.tuners_[1].get()), nullptr);
}

TEST(IRQAnomalyAnalyzerTest, AnalyzeAllApplicable)
{
    IRQAnomalyAnalyzer analyzer;

    analyzer.tuners_.clear();
    auto tuner1 = std::make_shared<GICTuner>();
    auto tuner2 = std::make_shared<GICTuner>();

    analyzer.tuners_.push_back(tuner1);
    analyzer.tuners_.push_back(tuner2);

    std::vector<std::string> data;
    auto applicable = analyzer.analyze(data);

    EXPECT_EQ(applicable.size(), 2);
}

TEST(IRQAnomalyAnalyzerTest, AnalyzeNoneApplicable)
{
    IRQAnomalyAnalyzer analyzer;

    analyzer.tuners_.clear();

    std::vector<std::string> data;
    auto applicable = analyzer.analyze(data);

    EXPECT_EQ(applicable.size(), 0);
}