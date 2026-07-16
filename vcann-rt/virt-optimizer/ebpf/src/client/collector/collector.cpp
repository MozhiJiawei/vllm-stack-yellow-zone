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

#include "collector.h"

#include <thread>

#include "log/ebpf_logger_macros.h"

void Collector::collectionTask()
{
    while (!stopFlag) {
        auto err = ring_buffer__poll(ringbuf.get(), 100);
        if (err < 0 && err != -EINTR) {
            EBPF_LOG_ERROR("Failed to read from ring buffer, error code: " + std::to_string(err));
            break;
        }
    }
    ringbuf.reset();
}

bool Collector::checkRunning() const
{
    return collecting_;
}

void Collector::stopCollecting()
{
    stopFlag = true;
    collectionThread_->join();
    collecting_ = false;
}
