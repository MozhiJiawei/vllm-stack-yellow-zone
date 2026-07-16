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

#include "write_combine_tuner.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

constexpr const char *targetFlag = "feature_bar_mem=1";

std::string WriteCombineTuner::name() const
{
    return "Write Combine";
}

std::string WriteCombineTuner::category() const
{
    return "IO BOUND";
}

std::string WriteCombineTuner::principle() const
{
    return "The physical machine supports small-packet PCIe BAR space copying, but the virtual machine does not. As a "
           "result, asynchronous DMA copying is used, which takes longer and leads to degraded H2D/D2H small-packet "
           "data copy performance.";
}

std::string WriteCombineTuner::advice() const
{
    return "Enable write combine. Data written to the WC region is temporarily stored in a 64-byte buffer. When the "
           "buffer is full or a refresh event is triggered, a merge write is executed to enhance throughput.";
}

bool WriteCombineTuner::check()
{
    EBPF_LOG_INFO("Checking write combine tunner.");
    isLastCheckSuccess = true;
    std::string directoryName = getCurrentTimeString();
    std::ostringstream oss;
    oss << "cd /root && msnpureport && "
        << R"(grep -nr \"Device capability info\" /root/)" << directoryName << "* && "
        << "rm -rf " << directoryName << "*";

    std::string cmd = oss.str();
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(cmd);
    if (result.first) {
        return result.second.find(targetFlag) == std::string_view::npos;
    } else {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Fail to execute cmd of check WriteCombine.");
    }
    return true;
}

void WriteCombineTuner::apply()
{
    std::cout << "1. Please enable the WriteCombine feature within the virtual machine "
                 "according to the provided PATCH."
              << std::endl
              << "2. Ensure that the HostOS and Qemu are compatible with the modifications in "
                 "the PATCH, and install the latest NPU HDK driver within the virtual machine."
              << std::endl;
}

std::string WriteCombineTuner::getCurrentTimeString()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&now_c, &localTime);

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d-%H");
    return oss.str();
}
