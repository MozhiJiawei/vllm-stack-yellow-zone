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

#ifndef OPT_ENGINE_H
#define OPT_ENGINE_H

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "analyzer/base_analyzer.h"

using DataStr = std::vector<std::string>;

class OPTEngine {
public:
    explicit OPTEngine(std::vector<std::unique_ptr<BaseAnalyzer<DataStr>>> analyzers);

    void run(const std::vector<std::string> &data);

private:
    std::vector<std::unique_ptr<BaseAnalyzer<DataStr>>> analyzers_;
};

#endif // OPT_ENGINE_H