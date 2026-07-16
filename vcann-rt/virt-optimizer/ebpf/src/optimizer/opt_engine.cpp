/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "opt_engine.h"
#include "log/ebpf_logger_macros.h"
#include "util/ui_manager.h"

OPTEngine::OPTEngine(std::vector<std::unique_ptr<BaseAnalyzer<std::vector<std::string>>>> analyzers)
    : analyzers_(std::move(analyzers))
{
}

void OPTEngine::run(const std::vector<std::string> &data)
{
    // 1. Perform analysis
    std::vector<std::shared_ptr<BaseTuner>> allSuggestions;
    for (const auto &analyzer : analyzers_) {
        auto suggestions = analyzer->analyze(data);
        allSuggestions.insert(allSuggestions.end(), suggestions.begin(), suggestions.end());
    }

    if (allSuggestions.empty()) {
        EBPF_LOG_INFO("No optimization options were found by any analyzers.");
        std::cout << "No optimization options were found by any analyzers.\n";
        return;
    }

    // 2. User interaction
    UIManager ui;
    ui.displaySuggestions(allSuggestions);
    auto selection = ui.getUserSelection(allSuggestions.size());
    if (selection.empty()) {
        std::cout << "No optimization options selected.\n";
        return;
    }

    // 3. Perform optimization
    ui.executeSelected(allSuggestions, selection);
}