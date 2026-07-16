/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "proc_helper.h"

#include <csignal>
#include <iostream>

#include "logger.h"
#include "string_util.h"

namespace vas::sched::acquire {
using namespace vas::common;
const fs::path ProcHelper::procPathPrefix = "/proc";
const std::regex ProcHelper::vCPUCommP = std::regex(R"(CPU\s+(\d+)\/KVM)");

/**
 * Retrieve the list of virtual machine process information
 * @param uuids vm uuid list
 * @return
 */
Vm2VcpuMap ProcHelper::GetVmProcList(const std::vector<std::string> &uuids)
{
    std::unique_lock lock(mutex);
    ResetProcInfo();
    auto ret = TraverseProcWithCond(uuids);
    if (isVasRetFail(ret)) {
        return vm2VcpuMap;
    }
    for (auto &[uuid, pid] : uuid2PidMap) {
        Vcpu2PidMap vcpu2PidMap{};
        if (ret = GetVcpuList(pid, vcpu2PidMap); isVasRetFail(ret)) {
            continue;
        }
        vm2VcpuMap[uuid] = vcpu2PidMap;
    }
    return vm2VcpuMap;
}

/**
 * Traverse the proc directory and obtain the virtual machine's PID
 * based on the UUID information contained in the cmdline file
 * @param uuids uuid list
 */
VasRet ProcHelper::TraverseProcWithCond(const std::vector<std::string> &uuids)
{
    if (!exists(procPathPrefix) || !is_directory(procPathPrefix)) {
        LOG_ERROR("Proc is not exists or is not a dir. path=" + procPathPrefix.string());
        return VAS_ERROR;
    }

    for (auto &dir : fs::directory_iterator(procPathPrefix)) {
        const fs::path cmdLine = dir.path() / "cmdline";
        if (!is_directory(dir.path()) || !exists(cmdLine)) {
            continue;
        }
        if (access(dir.path().c_str(), R_OK) == -1 || access(cmdLine.c_str(), R_OK) == -1) {
            continue;
        }
        std::ifstream file(cmdLine);
        if (!file.is_open()) {
            LOG_WARN("Open pid cmdline file failed. dir=" + dir.path().string());
            continue;
        }
        std::string line{};
        while (std::getline(file, line)) {
            for (auto &uuid : uuids) {
                if (line.find(uuid) == std::string::npos) {
                    continue;
                }
                try {
                    uuid2PidMap[uuid] = StringUtil::StringToPidt(dir.path().filename().c_str());
                } catch (const std::exception &e) {
                    LOG_WARN("Pid str to pid_t failed. err: " + std::string(e.what()));
                }
                break;
            }
        }
    }
    return VAS_OK;
}

void ProcHelper::ResetProcInfo()
{
    uuid2PidMap.clear();
    vm2VcpuMap.clear();
}

/**
 * Retrieve the virtual machine vCPU thread ID from the process file.
 * @param pid vm process ID
 * @param vcpu2PidMap
 */
VasRet ProcHelper::GetVcpuList(const pid_t &pid, Vcpu2PidMap &vcpu2PidMap)
{
    fs::path task = procPathPrefix / std::to_string(pid) / "task";
    if (!exists(task) || !is_directory(task)) {
        LOG_ERROR("Pid task dir not exists or is not dir. pid=" + std::to_string(pid));
        return VAS_ERROR;
    }
    for (auto &dir : fs::directory_iterator(task)) {
        const fs::path comm = dir.path() / "comm";
        if (access(dir.path().c_str(), R_OK) == -1 || access(comm.c_str(), R_OK) == -1) {
            continue;
        }
        std::ifstream file(comm);
        if (!file.is_open()) {
            LOG_WARN("Open task comm file failed. task=" + dir.path().string() + ",pid=" + std::to_string(pid));
            continue;
        }
        std::string line{};
        while (std::getline(file, line)) {
            if (std::smatch match; std::regex_search(line, match, vCPUCommP)) {
                try {
                    auto tid = StringUtil::StringToPidt(dir.path().filename().c_str());
                    auto vcpu = StringUtil::StringToUint16(match.str(1).c_str());
                    vcpu2PidMap[vcpu] = tid;
                } catch (const std::exception &e) {
                    LOG_WARN("Tid or vcpu str to uint16 failed. err: " + std::string(e.what()));
                }
            }
        }
    }
    return VAS_OK;
}
} // namespace vas::sched::acquire