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

#ifndef CLUSTER_VM_H
#define CLUSTER_VM_H

#include <list>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "dynamic_bitset.h"
#include "string_util.h"

namespace vas::common {
using PidVcpuMap = std::map<pid_t, uint16_t>;
using Vcpu2CpuMap = std::map<uint16_t, DynamicBitset>;

// Cluster layer
struct ClusterLayer {
    ClusterLayer() = default;

    uint16_t idle{};                // number of idle CPUs
    uint16_t total{};               // number of total CPUs
    DynamicBitset usedBitmap{};     // the CPU bitmap has been used.
                                    // For example: 0x03 indicates that CPUs 1 and 2 among CPUs 0-7 are in use.
    std::set<std::string> groups{}; // groupIds to which the cluster belongs.

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"idle":)" << idle << R"(,)";
        oss << R"("total":)" << total << R"(,)";
        oss << R"("usedBitmap":)" << Bitset::DynamicBitsetToStr(usedBitmap) << R"(,)";
        oss << R"("groups":)" << StringUtil::SetToStr(groups) << R"(})";
        return oss.str();
    }
};

// Cluster info
struct Cluster {
    Cluster() = default;

    uint16_t id{};                             // clusterId
    uint16_t numaId{};                         // numaId
    std::set<uint16_t> cpuSet{};               // the collection of all CPUs. For example: [8,9,...,15] represents
                                               // that the CPU range of this Cluster is 8-15.
    std::vector<ClusterLayer> clusterLayers{}; // cluster layer in overcommit case

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"id":)" << id << R"(,)";
        oss << R"("numaId":)" << numaId << R"(,)";
        oss << R"("cpuSet":)" << StringUtil::SetToStr(cpuSet) << R"(,)";
        oss << R"("clusterLayers":)" << StringUtil::ObjVecToStr(clusterLayers) << R"(})";
        return oss.str();
    }

    int32_t GetStartCpu() const
    {
        if (cpuSet.empty()) {
            return -1;
        }
        return *cpuSet.begin();
    }
};

// VM management instance on a NUMA node
struct VmDomain {
    VmDomain() = default;

    std::string uuid{};             // vm uuid
    std::string name{};             // vm name
    uint16_t numaId{};              // numaId for vm
    pid_t tgid{};                   // thread group ID, i.e., process ID
    PidVcpuMap pidVcpuMap{};        // CPU Thread List
    DynamicBitset commonCpuMap{};   // vcpu common available dynamic bitset
    std::set<std::string> groups{}; // groupId for vm
    std::set<pid_t> entityPids{};   // pointing to entity PID, range binding record
    bool isReScheded = false;       // rescheduled or not

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"uuid":")" << uuid << R"(",)";
        oss << R"("numaId":)" << numaId << R"(,)";
        oss << R"("tgid":)" << tgid << R"(,)";
        oss << R"("groups":)";
        oss << StringUtil::SetToStr(groups);
        oss << R"(})";
        return oss.str();
    }

    static DynamicBitset GetCommonCpuMap(const Vcpu2CpuMap &vcpu2CpuMap)
    {
        const auto totalSize = vcpu2CpuMap.begin()->second.size();
        auto ret = Bitset::GenDynamicBitSetByArea(totalSize, 0, totalSize);
        for (auto &[vcpu, cpuMap] : vcpu2CpuMap) {
            Bitset::DynamicBitsetAnd(ret, cpuMap);
        }
        return ret;
    }

    bool operator==(const VmDomain &rhs) const
    {
        return uuid == rhs.uuid;
    }
};

// The cluster management instance for VM.
struct VmGroup {
    VmGroup() = default;

    std::string domainKey{};      // uuid_numaId
    std::string id{};             // groupId: uuid_numaId_clusterId_layerId
    uint16_t clusterId{};         // clusterId
    uint8_t layerId{};            // layerId in cluster
    int start{};                  // cpu blinding starting index
    int nrCpus{};                 // cpu blinding number
    DynamicBitset usedBitmap{};   // the CPU bitmap has been used.
                                  // For example: 0x03 indicates that CPUs 1 and 2 among CPUs 0-7 are in use.
    std::set<pid_t> entityPids{}; // pointing to entity PID; 1:1 core binding record

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"id":")" << id << R"(",)";
        oss << R"("clusterId":)" << clusterId << R"(,)";
        oss << R"("layerId":)" << std::to_string(layerId) << R"(,)";
        oss << R"("start":)" << start << R"(,)";
        oss << R"("nrCpus":)" << nrCpus << R"(,)";
        oss << R"("usedBitmap":)";
        for (const auto bit : usedBitmap) {
            oss << (bit ? 1 : 0);
        }
        oss << R"(,)";
        oss << R"("entityPids":[)";
        for (auto it = entityPids.begin(); it != entityPids.end(); ++it) {
            oss << *it;
            if (std::next(it) != entityPids.end()) {
                oss << R"(,)";
            }
        }
        oss << R"(]})";
        return oss.str();
    }
};

// Running instance: vCPU bound to CPU
struct VmEntity {
    VmEntity() = default;

    pid_t pid{};  // vCPU thread ID
    int cpuIdx{}; // cpu index for 1:1 binding

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"pid":)" << pid << R"(",)";
        oss << R"("cpuIdx":)" << cpuIdx << R"(})";
        return oss.str();
    }
};

struct VcpuAffinityInfo {
    VcpuAffinityInfo() = default;

    uint16_t vcpu{}; // vcpuId
    pid_t pid{};     // vcpu thread id
    int cpuIdx = -1; // cpu index

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"vcpu":)" << vcpu << R"(,)";
        oss << R"("pid":)" << pid << R"(,)";
        oss << R"("cpuIdx":)" << cpuIdx << R"(})";
        return oss.str();
    }
};

struct VmDomainAffinity {
    VmDomainAffinity() = default;

    uint16_t numaId{};                                          // numaId for vm
    std::list<VmGroup> groups{};                                // groupId for vm
    std::map<uint16_t, VcpuAffinityInfo> vcpuAffinityInfoMap{}; // vCPU affinity config

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"numaId":)" << numaId << R"(,)";
        oss << R"("groups":[)";
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            oss << it->ToStr();
            if (std::next(it) != groups.end()) {
                oss << R"(,)";
            }
        }
        oss << R"(],)";
        oss << R"("vcpuAffinityInfoMap":{)";
        for (auto it = vcpuAffinityInfoMap.begin(); it != vcpuAffinityInfoMap.end(); ++it) {
            oss << R"(")" << it->first << R"(":)" << it->second.ToStr();
            if (std::next(it) != vcpuAffinityInfoMap.end()) {
                oss << R"(,)";
            }
        }
        oss << R"(})";
        oss << R"(})";
        return oss.str();
    }
};

struct VmAffinity {
    VmAffinity() = default;

    std::string uuid{};                                                 // vm uuid
    std::string name{};                                                 // vm name
    pid_t tgid{};                                                       // thread group ID, i.e., process ID
    std::unordered_map<uint16_t, VmDomainAffinity> domainAffinityMap{}; // NUMA granularity affinity configuration

    std::string ToStr() const
    {
        std::ostringstream oss;
        oss << R"({"uuid":)" << uuid << R"(,)";
        oss << R"("name":)" << name << R"(,)";
        oss << R"("tgid":)" << tgid << R"(,)";
        oss << R"("domainAffinityMap":{)";
        for (auto it = domainAffinityMap.begin(); it != domainAffinityMap.end(); ++it) {
            oss << R"(")" << it->first << R"(":)" << it->second.ToStr();
            if (std::next(it) != domainAffinityMap.end()) {
                oss << R"(,)";
            }
        }
        oss << R"(}})";
        return oss.str();
    }
};

using NumaClusterMap = std::map<uint16_t, std::map<uint16_t, Cluster>>;  // key numaId, clusterId
using DomainMap = std::unordered_map<std::string, VmDomain>;             // key uuid_numaId
using GroupMap = std::unordered_map<std::string, VmGroup>;               // key groupId
using EntityMap = std::unordered_map<pid_t, VmEntity>;                   // key pid
using NumaUsedCpuMap = std::map<uint16_t, std::map<uint16_t, uint16_t>>; // key layerId, numaId

/**
 * vmInfo aquire struct
 */
struct VcpuInfo {
    pid_t pid{};
    std::set<uint16_t> numaSet{};
    DynamicBitset cpuMaps{};
};

using VcpuNumaMap = std::unordered_map<uint16_t, VcpuInfo>;

struct IoThreadInfo {
    pid_t pid{};
    DynamicBitset ioThreadCpuMaps{};
};

using IoThreadMap = std::unordered_map<uint16_t, IoThreadInfo>;

struct VmInfo {
    VmInfo() = default;

    std::string uuid{};              // vm uuid
    std::string name{};              // vm name
    pid_t tgid{};                    // main process
    DynamicBitset emulatorCpuMaps{}; // Emulator cpumaps
    IoThreadMap ioThreadMap{};       // IO Thread ID Map
    VcpuNumaMap vcpuMap{};           // Child Thread Map
};
using VmInfoMap = std::unordered_map<std::string, VmInfo>; // key uuid
} // namespace vas::common

#endif // CLUSTER_VM_H
