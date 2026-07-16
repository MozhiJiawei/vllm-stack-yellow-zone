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

#ifndef BASE_ANALYZER_H
#define BASE_ANALYZER_H

#include <memory>
#include <vector>

#include "../tuner/base_tuner.h"

template <typename T>
class BaseAnalyzer {
public:
    virtual ~BaseAnalyzer() = default;
    virtual std::vector<std::shared_ptr<BaseTuner>> analyze(const T &data) const = 0;
};

#endif // BASE_ANALYZER_H