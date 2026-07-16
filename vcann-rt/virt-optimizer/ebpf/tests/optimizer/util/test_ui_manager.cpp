/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "optimizer/tuner/base_tuner.h"
#include "optimizer/tuner/gic_tuner.h"
#include "optimizer/util/ui_manager.h"

TEST(UIManagerTest, DisplaySuggestionsTest)
{
    UIManager uiManager;
    std::vector<std::shared_ptr<BaseTuner>> suggestions;

    auto tuner1 = std::make_shared<GICTuner>();
    auto tuner2 = std::make_shared<GICTuner>();

    suggestions.push_back(tuner1);
    suggestions.push_back(tuner2);

    uiManager.displaySuggestions(suggestions);
    EXPECT_EQ(suggestions.size(), 2);
}

TEST(UIManagerTest, GetUserSelectionAllTest)
{
    UIManager uiManager;
    std::set<size_t> selection;
    std::string input = "all";

    auto originalCinBuf = std::cin.rdbuf();
    std::istringstream inputStream(input);
    std::cin.rdbuf(inputStream.rdbuf());

    selection = uiManager.getUserSelection(2);

    EXPECT_EQ(selection.size(), 2);
    EXPECT_TRUE(selection.count(1));
    EXPECT_TRUE(selection.count(2));

    std::cin.rdbuf(originalCinBuf);
}

TEST(UIManagerTest, GetUserSelectionEffectiveTest)
{
    UIManager uiManager;
    std::set<size_t> selection;
    std::string input = "1,2";

    auto originalCinBuf = std::cin.rdbuf();
    std::istringstream inputStream(input);
    std::cin.rdbuf(inputStream.rdbuf());

    selection = uiManager.getUserSelection(2);

    EXPECT_EQ(selection.size(), 2);
    EXPECT_TRUE(selection.count(1));
    EXPECT_TRUE(selection.count(2));

    std::cin.rdbuf(originalCinBuf);
}

TEST(UIManagerTest, GetUserSelectionInvalidTest)
{
    UIManager uiManager;
    std::set<size_t> selection;
    std::string input = "0,3";

    auto originalCinBuf = std::cin.rdbuf();
    std::istringstream inputStream(input);
    std::cin.rdbuf(inputStream.rdbuf());

    selection = uiManager.getUserSelection(2);

    EXPECT_EQ(selection.size(), 0);

    std::cin.rdbuf(originalCinBuf);
}

TEST(UIManagerTest, ExecuteSelectedTest)
{
    UIManager uiManager;
    std::vector<std::shared_ptr<BaseTuner>> suggestions;

    auto tuner1 = std::make_shared<GICTuner>();
    auto tuner2 = std::make_shared<GICTuner>();

    suggestions.push_back(tuner1);
    suggestions.push_back(tuner2);

    std::set<size_t> selection = {1, 2};

    testing::internal::CaptureStdout();
    uiManager.executeSelected(suggestions, selection);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("Start Optimization..."), std::string::npos);
    EXPECT_NE(output.find("Optimization completed."), std::string::npos);
}