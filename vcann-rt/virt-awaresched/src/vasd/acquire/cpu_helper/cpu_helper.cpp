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

#include "cpu_helper.h"

#include <fstream>
#include <shared_mutex>
#include <unordered_map>

#include "dynamic_bitset.h"
#include "logger.h"
#include "string_util.h"
#include "vasd_arg_parse.h"

namespace vas::sched::acquire {
using namespace vas::common;
const fs::path CpuHelper::CPU_PATH_PREFIX = "/sys/devices/system/cpu";
const fs::path CpuHelper::NODE_PREFIX = "/sys/devices/system/node";
const std::regex CpuHelper::CPU_PATH_P(R"(cpu(\d+))");
const std::regex CpuHelper::NODE_PATH_P(R"(node(\d+))");
constexpr int CPU_ALIGNMENT_MASK = 7;
const uint16_t CpuHelper::CLUSTER_CPU_NUM = GetClusterCpuNum();
const uint16_t CpuHelper::MAX_CPU_NUM = GetMaxCpuNum();

VasRet CpuHelper::Init()
{
    if (CLUSTER_CPU_NUM == 0 || MAX_CPU_NUM == 0) {
        LOG_ERROR("get CLUSTER_CPU_NUM or MAX_CPU_NUM error, CLUSTER_CPU_NUM=" + std::to_string(CLUSTER_CPU_NUM) +
                  ", MAX_CPU_NUM=" + std::to_string(MAX_CPU_NUM));
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * cpu topology info acquisition
 */
CpuTopologyMap CpuHelper::GenCpuTopology()
{
    std::unique_lock lock(cpuInfolock);
    ResetCpuInfo();
    CpuInfoList cpuInfoList{};
    TraverseCpu(cpuInfoList, offlineCpuSet);
    GetCpuTopologyByCpuInfoList(cpuInfoList, cpuTopologyMap);
    return cpuTopologyMap;
}

CpuTopologyMap CpuHelper::GetCpuTopology()
{
    std::shared_lock lock(cpuInfolock);
    return cpuTopologyMap;
}

Cpu2NumaIdMap CpuHelper::GetCpu2NumaIdMap()
{
    std::shared_lock lock(cpuInfolock);
    return cpu2NumaIdMap;
}

Numa2CpusetMap CpuHelper::GetNuma2CpusetMap()
{
    std::shared_lock lock(cpuInfolock);
    return numaCpusetMap;
}

void CpuHelper::ResetCpuInfo()
{
    cpu2NumaIdMap.clear();
    cpuTopologyMap.clear();
    numaCpusetMap.clear();
    offlineCpuSet.clear();
    smtCpuNr = 0;
}

uint16_t CpuHelper::GetSmtCpuNr()
{
    std::shared_lock lock(cpuInfolock);
    return smtCpuNr;
}

/**
 * traverse cpu topology dir to get cpu info
 * @param cpuInfoList
 * @param offLineCpuSet
 */
void CpuHelper::TraverseCpu(CpuInfoList &cpuInfoList, std::set<uint16_t> &offLineCpuSet)
{
    for (auto &dir : fs::directory_iterator(CPU_PATH_PREFIX)) {
        std::string dirName(dir.path().filename().c_str());
        std::smatch match;
        if (!std::regex_search(dirName, match, CPU_PATH_P)) {
            continue;
        }
        try {
            auto cpuId = StringUtil::StringToUint16(match.str(1).c_str());
            if (IsCpuOffline(cpuId)) {
                offLineCpuSet.emplace(cpuId);
                continue;
            }
            CpuInfo cpuInfo = GetCpuInfo(cpuId);
            cpuInfoList.emplace_back(cpuInfo);
            cpu2NumaIdMap[cpuId] = cpuInfo.numaId;
            UpdateNumaCpusetMap(cpuInfo.numaId, cpuId);
        } catch (const std::exception &e) {
            LOG_ERROR(e.what());
        }
    }
    try {
        smtCpuNr = GetThreadSiblings(0);
    } catch (const std::exception &e) {
        LOG_ERROR(e.what());
    }
    LOG_INFO("CpuInfoList=" + StringUtil::ObjVecToStr(cpuInfoList) +
             "; offlineCpuSet=" + StringUtil::SetToStr(offLineCpuSet));
}

CpuInfo CpuHelper::GetCpuInfo(uint16_t cpuId)
{
    return {
        .numaId = GetNumaId(cpuId),
        .cpuId = cpuId,
        .clusterId = GetClusterId(cpuId),
        .clusterCpuSet = GetClusterCpuSet(cpuId),
    };
}

void CpuHelper::UpdateNumaCpusetMap(uint16_t numaId, uint16_t cpuId)
{
    if (numaCpusetMap.find(numaId) == numaCpusetMap.end()) {
        numaCpusetMap[numaId] = CpuSet{cpuId};
    } else {
        numaCpusetMap[numaId].emplace(cpuId);
    }
}

/**
 * save cpuInfo to ClusterInfoMap
 * @param cpuInfoList
 * @param topologyMap
 * @return
 */
void CpuHelper::GetCpuTopologyByCpuInfoList(const CpuInfoList &cpuInfoList, CpuTopologyMap &topologyMap)
{
    DynamicBitset clusterCpuMask(MAX_CPU_NUM);
    DynamicBitset skipCpuMask(MAX_CPU_NUM);
    bool hasSkipCpuSet = false;
    const auto skipCpuSet = StringUtil::ParseStringRange(VasdArgParse::skippedCPUSet);
    if (!skipCpuSet.empty()) {
        hasSkipCpuSet = true;
        skipCpuMask = Bitset::GenDynamicBitsetByCpuSet(MAX_CPU_NUM, skipCpuSet);
    }
    for (const auto &[numaId, cpuId, clusterId, clusterCpuSet] : cpuInfoList) {
        if (topologyMap.find(clusterId) != topologyMap.end()) {
            continue;
        }
        ClusterInfo cluster = ClusterInfo{
            .id = clusterId,
            .numaId = numaId,
            .total = static_cast<uint16_t>(clusterCpuSet.size()),
            .cpuSet = clusterCpuSet,
        };
        clusterCpuMask = Bitset::GenDynamicBitsetByCpuSet(MAX_CPU_NUM, clusterCpuSet);
        if (hasSkipCpuSet && Bitset::IsDynamicBitsetCross(clusterCpuMask, skipCpuMask)) {
            Bitset::DynamicBitsetAnd(clusterCpuMask, skipCpuMask);
            cluster.bitMap = Bitset::GenClusterBitSetByCpuSet(clusterCpuMask, clusterCpuSet);
            LOG_INFO("skipped cluster cpuSet=" + StringUtil::SetToStr(clusterCpuSet) +
                     ", skipped cluster cpuMask=" + Bitset::DynamicBitsetToStr(cluster.bitMap));
        } else {
            cluster.bitMap = DynamicBitset(CLUSTER_CPU_NUM);
        }
        topologyMap[clusterId] = cluster;
    }
}

/**
 * check whether cpu is offline
 * @param cpuId
 * @return true: cpu offline; false: cpu online
 */
bool CpuHelper::IsCpuOffline(const uint16_t &cpuId)
{
    return !exists(CPU_PATH_PREFIX / ("cpu" + std::to_string(cpuId)) / "topology");
}

/**
 * query cpu nodeId
 * @param cpuId
 * @return
 */
uint16_t CpuHelper::GetNumaId(const uint16_t &cpuId)
{
    const fs::path cpuFilePath = CPU_PATH_PREFIX / ("cpu" + std::to_string(cpuId));
    for (auto &dir : fs::directory_iterator(cpuFilePath)) {
        std::string dirName(dir.path().filename().c_str());
        if (std::smatch match; std::regex_search(dirName, match, NODE_PATH_P)) {
            return StringUtil::StringToUint16(match.str(1).c_str());
        }
    }
    throw std::runtime_error("Get cpu numa id failed. cpuId=" + std::to_string(cpuId));
}

uint16_t CpuHelper::GetClusterCpuNum() noexcept
{
    try {
        return static_cast<uint16_t>(GetClusterCpuSet(0).size());
    } catch (const std::exception &e) {
        LOG_ERROR(e.what());
        return static_cast<uint16_t>(0);
    }
}

/**
 * get Cluster cpulist
 * @param cpuId
 * @return
 */
CpuSet CpuHelper::GetClusterCpuSet(const uint16_t &cpuId)
{
    fs::path cpuFilPath = CPU_PATH_PREFIX / ("cpu" + std::to_string(cpuId)) / "topology" / "cluster_cpus_list";
    std::ifstream file(cpuFilPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + cpuFilPath.string() + ", cpuId=" + std::to_string(cpuId));
    }
    std::string line;
    while (std::getline(file, line)) {
        return StringUtil::ParseStringRange(line);
    }
    throw std::runtime_error("Cluster cpuList parse failed. cpuId=" + std::to_string(cpuId));
}

/**
 * get Cluster id
 * @param cpuId
 * @return
 */
uint16_t CpuHelper::GetClusterId(const uint16_t &cpuId)
{
    fs::path cpuFilPath = CPU_PATH_PREFIX / ("cpu" + std::to_string(cpuId)) / "topology" / "cluster_id";
    std::ifstream file(cpuFilPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + cpuFilPath.string() + ", cpuId=" + std::to_string(cpuId));
    }
    std::string line;
    while (std::getline(file, line)) {
        return StringUtil::StringToUint16(line.c_str());
    }
    throw std::runtime_error("Cluster id parse failed. cpuId=" + std::to_string(cpuId));
}

/**
 * get smt cpu number
 * @param cpuId
 * @return
 */
uint16_t CpuHelper::GetThreadSiblings(const uint16_t &cpuId)
{
    fs::path cpuFilPath = CPU_PATH_PREFIX / ("cpu" + std::to_string(cpuId)) / "topology" / "thread_siblings_list";
    std::ifstream file(cpuFilPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + cpuFilPath.string() + ", cpuId=" + std::to_string(cpuId));
    }
    std::string line;
    while (std::getline(file, line)) {
        return StringUtil::ParseStringRange(line).size();
    }
    throw std::runtime_error("Thread siblings list parse failed, cpuId=" + std::to_string(cpuId));
}

/**
 * Get Cpu total count by numa cpulist
 * @return
 */
uint16_t CpuHelper::DoGetMaxCpuNum()
{
    std::set<uint16_t> cpuSet;
    for (auto &dir : fs::directory_iterator(NODE_PREFIX)) {
        std::string dirName(dir.path().filename().c_str());
        std::smatch match;
        if (!std::regex_search(dirName, match, NODE_PATH_P)) {
            continue;
        }
        try {
            std::string numaId = match.str(1);
            fs::path cpuFilPath = NODE_PREFIX / ("node" + numaId) / "cpulist";
            std::ifstream file(cpuFilPath);
            if (!file.is_open()) {
                LOG_WARN("Numa cpulist open failed, path=" + cpuFilPath.string() + ", numaId=" + numaId);
                continue;
            }
            std::set<uint16_t> numaSet;
            std::stringstream buffer;
            buffer << file.rdbuf();
            numaSet = StringUtil::ParseStringRange(buffer.str());
            cpuSet.insert(numaSet.begin(), numaSet.end());
        } catch (const std::exception &e) {
            LOG_ERROR(e.what());
        }
    }
    if (cpuSet.empty()) {
        return 0;
    }
    return (cpuSet.size() + CPU_ALIGNMENT_MASK) & ~CPU_ALIGNMENT_MASK; // aligned to 8
}

uint16_t CpuHelper::GetMaxCpuNum() noexcept
{
    try {
        return static_cast<uint16_t>(DoGetMaxCpuNum());
    } catch (const std::exception &e) {
        LOG_ERROR(e.what());
        return static_cast<uint16_t>(0);
    }
}
} // namespace vas::sched::acquire
