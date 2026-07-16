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

#include "optimizer/analyzer/base_analyzer.h"
#include "optimizer/analyzer/cpu_bound_analyzer.h"
#include "optimizer/analyzer/io_bound_analyzer.h"
#include "optimizer/analyzer/irq_anomaly_analyzer.h"
#include "optimizer/opt_engine.h"
#include "optimizer/util/ui_manager.h"

TEST(OPTEngineTest, RUNCase1)
{
    std::vector<std::unique_ptr<BaseAnalyzer<DataStr>>> analyzers;
    analyzers.push_back(std::make_unique<CPUBoundAnalyzer>());
    analyzers.push_back(std::make_unique<IOBoundAnalyzer>());
    analyzers.push_back(std::make_unique<IRQAnomalyAnalyzer>());

    OPTEngine engine(std::move(analyzers));
    std::vector<std::string> data;
    data.push_back("123");
    std::set<size_t> orig;
    MOCKER(&UIManager::displaySuggestions).stubs().with(any()).will(returnValue(nullptr));
    MOCKER(&UIManager::getUserSelection).stubs().with(any()).will(returnValue(orig));
    engine.run(data);
}

TEST(OPTEngineTest, RUNCase2)
{
    std::vector<std::unique_ptr<BaseAnalyzer<DataStr>>> analyzers;

    OPTEngine engine(std::move(analyzers));
    std::vector<std::string> data;
    data.push_back("123");
    engine.run(data);
}
