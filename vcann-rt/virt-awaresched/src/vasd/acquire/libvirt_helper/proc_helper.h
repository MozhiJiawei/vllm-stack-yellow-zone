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

#ifndef PROC_HELPER_H
#define PROC_HELPER_H

#include <filesystem>
#include <regex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "error.h"

namespace vas::sched::acquire {
using namespace vas::common;
namespace fs = std::filesystem;
using Uuid2PidMap = std::unordered_map<std::string, pid_t>;
using Vcpu2PidMap = std::unordered_map<uint16_t, pid_t>;
using Vm2VcpuMap = std::unordered_map<std::string, Vcpu2PidMap>; // key uuid

class ProcHelper {
public:
    static ProcHelper &GetInstance()
    {
        static ProcHelper instance;
        return instance;
    }

    Vm2VcpuMap GetVmProcList(const std::vector<std::string> &uuids);
    Uuid2PidMap GetUuid2PidMap()
    {
        std::shared_lock lock(mutex);
        return uuid2PidMap;
    };

private:
    static VasRet GetVcpuList(const pid_t &pid, Vcpu2PidMap &vcpu2PidMap);

    VasRet TraverseProcWithCond(const std::vector<std::string> &uuids);
    void ResetProcInfo();

    static const fs::path procPathPrefix;
    static const std::regex vCPUCommP;

    std::shared_mutex mutex{};
    Uuid2PidMap uuid2PidMap{};
    Vm2VcpuMap vm2VcpuMap{};
};
} // namespace vas::sched::acquire

#endif // PROC_HELPER_H
