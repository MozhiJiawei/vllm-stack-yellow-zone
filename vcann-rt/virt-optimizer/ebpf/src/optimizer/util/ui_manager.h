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

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "./tuner/base_tuner.h"

// User interface processor
class UIManager {
public:
    void displaySuggestions(const std::vector<std::shared_ptr<BaseTuner>> &suggestions) const;

    std::set<size_t> getUserSelection(size_t max) const;

    void executeSelected(const std::vector<std::shared_ptr<BaseTuner>> &suggestions,
                         const std::set<size_t> &selection) const;
};

#endif // UI_MANAGER_H