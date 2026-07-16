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

#ifndef IPI_COLLECTOR
#define IPI_COLLECTOR

#include "collector.h"

class IPICollector : public Collector {
public:
    static IPICollector &getInstance();

    IPICollector(const IPICollector &) = delete;

    IPICollector &operator=(const IPICollector &) = delete;

    CollectorStatus launchCollecting() override;

    void stopCollecting() override;

private:
    IPICollector() = default;
    struct ipi_trace *ipiTrace{nullptr};
};

#endif