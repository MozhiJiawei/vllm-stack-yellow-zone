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

#include "io_bound_analyzer.h"

#include "../tuner/huge_page_tuner.h"
#include "../tuner/write_combine_tuner.h"

IOBoundAnalyzer::IOBoundAnalyzer()
{
    tuners_.push_back(std::make_shared<HugePageTuner>());
    tuners_.push_back(std::make_shared<WriteCombineTuner>()); // More optimizers can be added
}

std::vector<std::shared_ptr<BaseTuner>> IOBoundAnalyzer::analyze(const std::vector<std::string> &data) const
{
    std::vector<std::shared_ptr<BaseTuner>> applicable;
    for (const auto &tuner : tuners_) {
        if (tuner->check()) {
            applicable.push_back(tuner);
        }
    }
    return applicable;
}