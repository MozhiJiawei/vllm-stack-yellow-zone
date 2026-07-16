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

#include "ebpf_receiver.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"
#include "vsock_client.h"

const char *DATA_LOCAL_PATH = "/var/ubs-opt/data/data.json";

void EBPFReceiver::mainLoop()
{
    std::atomic_load(&dataBuffer)->clear();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(samplingInterval_));
        std::atomic_load(&readBuffer_)->clear();
        auto data = std::atomic_exchange(&dataBuffer, readBuffer_);
        if (!sendData(*std::atomic_load(&data))) {
            EBPF_LOG_WARN("Failed to send collected data. Data will be saved locally.");
            utils::writeToDisk({data->toJson()}, DATA_LOCAL_PATH);
        }
        atomic_store(&readBuffer_, data);
    }
}

void EBPFReceiver::launch()
{
    if (receiverThread_ != nullptr) {
        return;
    }
    receiverThread_ = std::make_unique<std::thread>(&EBPFReceiver::mainLoop, this);
}

void EBPFReceiver::setSamplingInterval(unsigned int newSamplingInterval)
{
    this->samplingInterval_ = newSamplingInterval;
}

EBPFReceiver &EBPFReceiver::getInstance()
{
    static EBPFReceiver eBPFReceiver;
    return eBPFReceiver;
}
