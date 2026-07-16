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

#include "cluster_topo_tuner.h"
#include <iostream>
#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

std::string ClusterTopoTuner::name() const
{
    return "Cluster Scheduling Optimization";
}

std::string ClusterTopoTuner::category() const
{
    return "CPU BOUND";
}

std::string ClusterTopoTuner::principle() const
{
    return "Cross-cluster CPU scheduling leads to slow card issues, multiple cards waiting for the slow card.";
}

std::string ClusterTopoTuner::advice() const
{
    return "Enable cluster scheduling optimization. Map the physical CPU cluster topology to virtual machines to "
           "achieve the same optimization effects within the virtual machine system as those of the physical CPU.";
}

bool ClusterTopoTuner::check()
{
    EBPF_LOG_INFO("Checking cluster topology tunner.");
    isLastCheckSuccess = true;
    std::string domainToCheck = "domain2";
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    CmdExecutor executor(hostName);
    std::string cmd = "echo 1 > /proc/sys/kernel/sched_cluster && cat /proc/schedstat";
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Check the Cluster flag file failed.");
    } else {
        if (result.second.find(domainToCheck) != std::string::npos) {
            return false; // User completes setup
        }
    }
    return true; // User has no settings
}

void ClusterTopoTuner::apply()
{
    std::cout << "Please enable cluster scheduling optimization." << std::endl;
}
