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

#ifndef BASETUNER_H
#define BASETUNER_H

#include <string>

// Base class for optimizer
class BaseTuner {
public:
    virtual ~BaseTuner() = default;
    virtual std::string name() const = 0;
    virtual std::string category() const = 0;
    virtual std::string principle() const = 0;
    virtual std::string advice() const = 0;

    virtual bool check() = 0;
    virtual void apply() = 0;

    bool isLastCheckSuccess;
};

#endif // BASETUNER_H