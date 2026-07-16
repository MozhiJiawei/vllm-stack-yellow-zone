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

#ifndef SCHED_COLLECTOR_H
#define SCHED_COLLECTOR_H

#include "collector.h"

class SchedCollector : public Collector {
public:
    static SchedCollector &getInstance();

    SchedCollector(const SchedCollector &) = delete;

    SchedCollector *operator=(const SchedCollector &) = delete;

    CollectorStatus launchCollecting() override;

private:
    SchedCollector() = default;

    void stopCollecting() override;

    struct sched_trace *schedTrace{nullptr};
};

#endif
