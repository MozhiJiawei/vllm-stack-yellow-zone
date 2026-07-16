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

#ifndef WRITE_COMBINE_TUNER_H
#define WRITE_COMBINE_TUNER_H

#include "base_tuner.h"

class WriteCombineTuner : public BaseTuner {
public:
    std::string name() const override;
    std::string category() const override;
    std::string principle() const override;
    std::string advice() const override;

    bool check() override;
    void apply() override;

    std::string getCurrentTimeString();
};

#endif // WRITE_COMBINE_TUNER_H