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

#ifndef CPU_HELPER_H
#define CPU_HELPER_H

#include <filesystem>
#include <regex>
#include <set>
#include <shared_mutex>

#include "dynamic_bitset.h"
#include "error.h"

namespace vas::sched::acquire {
namespace fs = std::filesystem;
using namespace vas::common;

using CpuSet = std::set<uint16_t>;
struct CpuInfo {
    CpuInfo() = default;

    uint16_t numaId{};
    uint16_t cpuId{};
    uint16_t clusterId{};
    CpuSet clusterCpuSet{};

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"numaId":)" << numaId;
        oss << R"(,"cpuId":)" << cpuId;
        oss << R"(,"clusterId":)" << clusterId;
        oss << R"(})";
        return oss.str();
    }
};
using CpuInfoList = std::vector<CpuInfo>;

struct ClusterInfo {
    ClusterInfo() = default;

    uint16_t id{};               // clusterId
    uint16_t numaId{};           // numaId
    uint16_t total{};            // cpu total number
    std::set<uint16_t> cpuSet{}; // CPU set in Cluster. example: [8,9,10,...,15]
    DynamicBitset bitMap;        // bitMap for skipping cluster

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"id":)" << id << R"(,)";
        oss << R"("numaId":)" << numaId << R"(,)";
        oss << R"("total":)" << total << R"(})";
        return oss.str();
    }

    bool operator==(const ClusterInfo &rhs) const
    {
        return id == rhs.id;
    }
};
using CpuTopologyMap = std::map<uint16_t, ClusterInfo>;
using Cpu2NumaIdMap = std::map<uint16_t, uint16_t>;
using Numa2CpusetMap = std::map<uint16_t, CpuSet>;

class CpuHelper {
public:
    static VasRet Init();
    static const uint16_t CLUSTER_CPU_NUM;
    static const uint16_t MAX_CPU_NUM;

    static CpuHelper &GetInstance()
    {
        static CpuHelper instance;
        return instance;
    }
    CpuTopologyMap GenCpuTopology();
    CpuTopologyMap GetCpuTopology();
    Cpu2NumaIdMap GetCpu2NumaIdMap();
    Numa2CpusetMap GetNuma2CpusetMap();
    uint16_t GetSmtCpuNr();

private:
    std::shared_mutex cpuInfolock{};
    Cpu2NumaIdMap cpu2NumaIdMap{};
    CpuTopologyMap cpuTopologyMap{};
    Numa2CpusetMap numaCpusetMap{};
    std::set<uint16_t> offlineCpuSet{};
    uint16_t smtCpuNr{};
    static const fs::path CPU_PATH_PREFIX;
    static const fs::path NODE_PREFIX;
    static const std::regex CPU_PATH_P;
    static const std::regex NODE_PATH_P;

    void ResetCpuInfo();
    void TraverseCpu(CpuInfoList &cpuInfoList, std::set<uint16_t> &offLineCpuSet);
    static void GetCpuTopologyByCpuInfoList(const CpuInfoList &cpuInfoList, CpuTopologyMap &topologyMap);
    static bool IsCpuOffline(const uint16_t &cpuId);
    static uint16_t GetNumaId(const uint16_t &cpuId);
    static CpuSet GetClusterCpuSet(const uint16_t &cpuId);
    static uint16_t GetClusterId(const uint16_t &cpuId);
    static uint16_t GetThreadSiblings(const uint16_t &cpuId);
    static uint16_t GetClusterCpuNum() noexcept;
    static uint16_t DoGetMaxCpuNum();
    static uint16_t GetMaxCpuNum() noexcept;
    static CpuInfo GetCpuInfo(uint16_t cpuId);
    void UpdateNumaCpusetMap(uint16_t numaId, uint16_t cpuId);
};

} // namespace vas::sched::acquire

#endif // CPU_HELPER_H
