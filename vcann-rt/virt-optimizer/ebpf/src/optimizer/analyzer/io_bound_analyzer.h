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

#ifndef IO_BOUND_ANALYZER_H
#define IO_BOUND_ANALYZER_H

#include <string>
#include <vector>

#include "base_analyzer.h"

class IOBoundAnalyzer : public BaseAnalyzer<std::vector<std::string>> {
public:
    IOBoundAnalyzer();
    std::vector<std::shared_ptr<BaseTuner>> analyze(const std::vector<std::string> &data) const override;

private:
    std::vector<std::shared_ptr<BaseTuner>> tuners_;
};

#endif // IO_BOUND_ANALYZER_H