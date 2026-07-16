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

#include "cpu_topo_tuner.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

const char *CPU_INFO_CMD = "virsh vcpuinfo";

std::string CPUTopoTuner::name() const
{
    return "CPU Core Binding";
}

std::string CPUTopoTuner::category() const
{
    return "CPU BOUND";
}

std::string CPUTopoTuner::principle() const
{
    return "The topology structure does not match the physical machine, resulting in high scheduling and memory access "
           "overhead.";
}

std::string CPUTopoTuner::advice() const
{
    return "Enable CPU core binding. Synchronize the topology of physical machine and virtual machine in pass-through "
           "scenarios.";
}

bool CPUTopoTuner::check()
{
    EBPF_LOG_INFO("Checking CPU topology tunner.");
    isLastCheckSuccess = true;
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        isLastCheckSuccess = false;
        return true;
    }
    std::ostringstream cmd;
    cmd << CPU_INFO_CMD << " " << hostName;
    CmdExecutor executor;
    auto result = executor.runCommand(cmd.str());
    if (!result.first) {
        EBPF_LOG_WARN(std::string("Run '") + CPU_INFO_CMD + std::string("' failed. Unable to check CPU Topology."));
        return true;
    }
    cpu2Vcpu.clear();
    return parse(result.second);
}

bool CPUTopoTuner::parse(const std::string &output)
{
    std::istringstream iss(output);
    std::string line;
    int vcpuID = -1;
    while (std::getline(iss, line)) {
        if (line.find("VCPU:") != std::string::npos) {
            if (vcpuID != -1) {
                EBPF_LOG_WARN("Libvirt CPU info in unknown format. Unable to check.");
                isLastCheckSuccess = false;
                return true;
            }
            if (vcpuID = getValue(line); vcpuID < 0) {
                EBPF_LOG_WARN("CPUID in unknown format: '" + line + "'. Unable to check.");
                isLastCheckSuccess = false;
                return true;
            }
        } else if (line.find("CPU:") != std::string::npos) {
            if (vcpuID == -1) {
                EBPF_LOG_WARN("Libvirt CPU info in unknown format. Unable to check.");
                isLastCheckSuccess = false;
                return true;
            }
            int cpuID;
            if (cpuID = getValue(line); cpuID < 0) {
                EBPF_LOG_INFO("vCPU Core " + std::to_string(vcpuID) + " bind in the discovery range.");
                return true;
            }
            if (auto it = cpu2Vcpu.find(cpuID); it != cpu2Vcpu.end()) {
                EBPF_LOG_INFO("CPU Core " + std::to_string(cpuID) + " bind with multi vCPU: " +
                              std::to_string(it->second) + ", " + std::to_string(vcpuID) + ".");
                return true;
            }
            cpu2Vcpu.insert(std::make_pair(cpuID, vcpuID));
            vcpuID = -1;
        }
    }
    return false;
}

int CPUTopoTuner::getValue(const std::string &line)
{
    std::istringstream iss(line);
    std::string tmp;
    std::getline(iss, tmp, ':');
    int ret;
    iss >> ret;
    if (!iss.eof() || iss.fail()) {
        return -1;
    }
    return ret;
}

void CPUTopoTuner::apply()
{
    std::cout << "1. Open VM's libvirt configuration XML." << std::endl
              << "2. Bind every vCPU to a single different CPU." << std::endl;
}
