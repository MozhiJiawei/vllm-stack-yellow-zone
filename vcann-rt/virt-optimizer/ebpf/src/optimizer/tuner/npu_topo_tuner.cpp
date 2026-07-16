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

#include "npu_topo_tuner.h"

#include <dirent.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "rapidjson/document.h"

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

std::string NPUTopoTuner::name() const
{
    return "NUMA NPU Binding";
}

std::string NPUTopoTuner::category() const
{
    return "CPU BOUND";
}

std::string NPUTopoTuner::principle() const
{
    return "The lack of affinity between the NPU and CPU leads to high scheduling and memory access overhead.";
}

std::string NPUTopoTuner::advice() const
{
    return "Enable NUMA NPU binding. Synchronize the NPU NUMA information to the virtual machine.";
}

bool NPUTopoTuner::check()
{
    EBPF_LOG_INFO("Checking NPU topology tunner.");
    isLastCheckSuccess = true;
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    std::string typeStr = utils::NPUType2Str(utils::getNPUType());
    if (typeStr.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    CmdExecutor executor(hostName);
    std::vector<std::string> pciIds;
    std::string cmd = "lspci | grep " + typeStr; // Whitelist filtering is done above to prevent command injection
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Check the PCI number of the NPU failed.");
    } else {
        std::regex pciIdRegex(R"((\w{2}:\w{2}\.\w))");
        std::smatch matches;
        std::string output = result.second;
        std::string::const_iterator searchStart(output.cbegin());
        while (std::regex_search(searchStart, output.cend(), matches, pciIdRegex)) {
            if (matches.size() > 1) {
                pciIds.push_back("0000:" + matches[1].str());
            }
            searchStart = matches[0].second;
        }
    }
    std::string infoFile = "numa_node";
    for (const auto &pciId : pciIds) {
        cmd = "cat /sys/bus/pci/devices/" + pciId + "/" + infoFile;
        result = executor.runCommand(cmd);
        if (!result.first) {
            isLastCheckSuccess = false;
            EBPF_LOG_ERROR("Check the NUMA flag of the NPU failed.");
            return true;
        } else {
            if (result.second.find("-") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

void NPUTopoTuner::apply()
{
    std::cout << "1. Check the mapping relationship between NPU and NUMA in the physical machine." << std::endl
              << "2. Check the virtual machine configuration file to clarify the mapping relationship\n"
                 " between the physical machine's NPU PCI and the virtual machine's PCI. "
              << std::endl
              << "3. Set the NUMA flag for the NPU in the virtual machine." << std::endl;
}