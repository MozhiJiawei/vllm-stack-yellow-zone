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

#ifndef CLUSTER_SCHED_H
#define CLUSTER_SCHED_H

#include <filesystem>
#include <shared_mutex>

#include "cluster_vm.h"
#include "error.h"
#include "libvirt_helper.h"

namespace vas::sched {
using namespace vas::common;
using namespace vas::sched::acquire;
namespace fs = std::filesystem;

enum class VmThreadType
{
    VCPU_CPUSET = 0,
    VCPU_PREFERRED_CPU,
};

class ClusterSched {
public:
    static ClusterSched &GetInstance()
    {
        static ClusterSched instance{};
        return instance;
    }

    VasRet UpdateDomainInfosAndSched();
    VasRet AddDomainInfo(const VmInfo &vmInfo);
    void DelDomainInfo(const std::string &uuid);
    VasRet InitClusterInfo();
    void GetAffinityInfo(const std::string &value, std::unordered_map<std::string, VmAffinity> &ret);
    std::vector<VmDomain> GetDomainsByUuid(const std::string &uuid);
    VasRet ReSchedVm(const std::string &value);
    void ClusterCompaction();
    void ReSchedStartedVms();
    void ReSetSchedPolicy();
    void RecoverVmVcpu(const VmInfoMap &vmInfoMap);

private:
    std::shared_mutex dataMutex_{};
    std::mutex transactionMutex_{};
    NumaClusterMap numaClusterMap_{};
    DomainMap domainMap_{};
    EntityMap entityMap_{};
    GroupMap groupMap_{};
    uint16_t compactionCount_{};
    std::unordered_map<uint16_t, uint8_t> overProvision_{};

    ClusterSched() = default;
    ~ClusterSched() = default;
    void GetVcpuAffinityInfoMap(VmDomain &domain, const std::list<VmGroup> &groups,
                                std::map<uint16_t, VcpuAffinityInfo> &vcpuAffinityInfoMap);
    void ClusterCompactionWithoutLock();
    void UpdateDomainInfosWithoutLock(const VmInfoMap &vmInfoMap);
    VasRet UpdateDomainInfoWithoutLock(const VmInfo &vmInfo, NumaUsedCpuMap &numaUsedCpuMap, VmDomain &vmDomain);
    uint16_t GetNumaCpuCount(const uint16_t &numaId);
    NumaUsedCpuMap GetNumaUsedCpuMap();
    VasRet SelectMinLayer(const std::set<uint16_t> &availableNumas, uint16_t &selectNumaId);
    VasRet SelectVmNuma(const VmInfo &vmInfo, NumaUsedCpuMap &numaUsedCpuMap, uint16_t &selectNumaId);
    VasRet Alloc(VmDomain &domain);
    void Free(VmDomain &domain);
    VasRet Assign(VmDomain &domain);
    void Unassign(VmDomain &domain);
    VasRet ReSched(std::vector<VmDomain> &domains);
    VasRet DeReSched(std::vector<VmDomain> &domains);
    uint16_t GetNumaTotalCpus(const uint16_t &numaId);
    uint16_t AllocClusterGroupToDomain(VmDomain &domain, const uint16_t &nr);
    uint16_t AllocGroupFromCluster(const uint16_t &nr, const uint8_t &layerId, VmDomain &domain, Cluster &cluster,
                                   DynamicBitset &availableCpuMap);
    DynamicBitset GetDomainCpuMask(const std::vector<VmDomain> &domains);
    VasRet AssignPidCpu(VmDomain &domain, const pid_t &pid);
    void UnAssignPidCpu(const VmDomain &domain);
    VasRet SetVcpuAffinity(VmDomain &domain, const pid_t &pid, const uint16_t &cpuIndex, const uint16_t &clusterId);
    void RestoreVmInfoWithoutLock();
    void ReSchedStartedVmsWithoutLock();
    VmEntity &GenEntity(const pid_t &pid, const int16_t &index = -1);
    VasRet CleanDyingPid();
    void CleanDyingPidByGroup(VmDomain &domain);
    void CompactionCluster(uint16_t numaId, std::map<uint16_t, Cluster> &clusterMap);
    void CompactionClusterOneLayer(std::map<uint16_t, Cluster> &clusterMap, const uint8_t &layerId);
    void CompactionGroupWithinCluster(Cluster &cluster, Cluster &nextCluster, const uint8_t &layerId);
    void CompactionGroupFromLastLayer(Cluster &cluster, const uint8_t &layerId);
    VasRet GroupEntityMigrate(VmGroup &group, const int16_t &newStart, const Cluster &cluster, const uint8_t &layerId);
    void OverProvisionUp(uint16_t numaId);
    void OverProvisionDown(uint16_t numaId);

    static const uint8_t maxOverProvision;

    static uint16_t GetGranularity();
    static void AddGroupToClusterLayer(const VmGroup &group, ClusterLayer &clusterLayer);
    static void DelGroupFromClusterLayer(const VmGroup &group, ClusterLayer &clusterLayer);
    static void AddGroupToDomain(const VmGroup &group, VmDomain &domain);
    static void DelGroupFromDomain(const VmGroup &group, VmDomain &domain);
    static void AddEntityToGroup(const VmEntity &entity, const uint16_t &granularity, VmGroup &group);
    static void RecoverVmVcpu_(const VmInfoMap &vmInfoMap);
    static VasRet GetVmCgroupPath(const std::string &uuid, fs::path &cpuPath);
    static VasRet SetVmCpuset(const fs::path &vmPathPre, const VmThreadType &threadType, const DynamicBitset &cpuBitSet,
                              const int32_t &id = 0);
};

} // namespace vas::sched

#endif // CLUSTER_SCHED_H
