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

#include "cluster_sched.h"

#include <chrono>
#include <regex>
#include <vector>

#include "cmd.h"
#include "cpu_helper.h"
#include "logger.h"
#include "string_util.h"
#include "vas_security_manager.h"
#include "vasd_arg_parse.h"

namespace vas::sched {
using namespace std::chrono;
using namespace vas::common;
using namespace vas::security;

const uint8_t ClusterSched::maxOverProvision = 6;

/**
 * update vm domain info
 * @return VasRet
 */
VasRet ClusterSched::UpdateDomainInfosAndSched()
{
    VmInfoMap vmInfoMap{};
    if (const auto ret = LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap); ret != VAS_OK) {
        LOG_ERROR("Acquire vm info list failed.");
        return ret;
    }
    std::unique_lock lock(dataMutex_);
    UpdateDomainInfosWithoutLock(vmInfoMap);
    std::vector<VmDomain> needAllocDomains{};
    for (auto &[uuid, domain] : domainMap_) {
        if (domain.isReScheded) {
            continue;
        }
        auto ret = Alloc(domain);
        if (isVasRetFail(ret)) {
            LOG_WARN("Alloc failed. uuid=" + domain.uuid);
            continue;
        }
        ret = Assign(domain);
        if (isVasRetFail(ret)) {
            LOG_WARN("Assign failed. uuid=" + domain.uuid);
            continue;
        }
        domain.isReScheded = true;
        needAllocDomains.emplace_back(domain);
    }
    if (needAllocDomains.empty()) {
        return VAS_OK;
    }
    return VAS_OK;
}

/**
 * Adding a VM
 * @param vmInfo vm information
 * @return VasRet VAS_OK success; others failed
 */
VasRet ClusterSched::AddDomainInfo(const VmInfo &vmInfo)
{
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock dataLock(dataMutex_);

    LOG_INFO("Add domain info.");
    auto numaUsedCpuMap = GetNumaUsedCpuMap();
    VmDomain vmDomain{};
    auto ret = UpdateDomainInfoWithoutLock(vmInfo, numaUsedCpuMap, vmDomain);
    if (isVasRetFail(ret)) {
        return VAS_ERROR;
    }
    // In the current allocation policy, VM CPUs are allocated to only the same NUMA node.
    // Data structures of multiple domains are generated during cross-NUMA allocation.
    std::vector vmDomains{vmDomain};
    ret = ReSched(vmDomains);
    if (isVasRetFail(ret)) {
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * Deleting a VM
 * @param uuid vm info
 */
void ClusterSched::DelDomainInfo(const std::string &uuid)
{
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock dataLock(dataMutex_);

    LOG_INFO("Delete domain info.");
    auto domains = GetDomainsByUuid(uuid);
    DeReSched(domains);
    ClusterCompactionWithoutLock();
}

/**
 * init cpu cluster info
 * @return VasRet
 */
VasRet ClusterSched::InitClusterInfo()
{
    LOG_INFO("Init cluster schedule handler start.");
    if (isVasRetFail(CpuHelper::Init())) {
        return VAS_ERROR;
    }
    CpuTopologyMap cpuTopologyMap = CpuHelper::GetInstance().GenCpuTopology();

    for (const auto &[clusterId, clusterInfo] : cpuTopologyMap) {
        std::vector<ClusterLayer> clusterLayers{};
        clusterLayers.emplace_back(ClusterLayer{
            .idle = static_cast<uint16_t>(std::count(clusterInfo.bitMap.begin(), clusterInfo.bitMap.end(), false)),
            .total = clusterInfo.total,
            .usedBitmap = clusterInfo.bitMap,
            .groups = std::set<std::string>{},
        });
        const auto cluster = Cluster{
            .id = clusterInfo.id,
            .numaId = clusterInfo.numaId,
            .cpuSet = clusterInfo.cpuSet,
            .clusterLayers = clusterLayers,
        };
        numaClusterMap_[clusterInfo.numaId][clusterInfo.id] = cluster;
    }

    for (const auto &[numaId, _] : numaClusterMap_) {
        overProvision_.emplace(numaId, 1);
    }
    LOG_INFO("Init cluster schedule handler end.");
    return VAS_OK;
}

/**
 * get vmDomains from uuid
 * @param uuid
 * @return std::vector<VmDomain>
 */
std::vector<VmDomain> ClusterSched::GetDomainsByUuid(const std::string &uuid)
{
    std::vector<VmDomain> ret{};
    for (auto &[domainKey, domain] : domainMap_) {
        if (domain.uuid == uuid) {
            ret.emplace_back(domain);
        }
    }
    return ret;
}

/**
 * cluster compaction
 */
void ClusterSched::ClusterCompaction()
{
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock dataLock(dataMutex_);
    ClusterCompactionWithoutLock();
}

/**
 * get cpu affinity info by uuid
 * @param value
 * @param ret VmAffinity map
 */
void ClusterSched::GetAffinityInfo(const std::string &value, std::unordered_map<std::string, VmAffinity> &ret)
{
    std::shared_lock lock(dataMutex_);
    for (auto &[domainKey, domain] : domainMap_) {
        if (value != "all" && value != domain.name && value != domain.uuid) {
            continue;
        }
        if (ret.find(domain.uuid) == ret.end()) {
            ret[domain.uuid] = VmAffinity{};
        }
        auto &[vmUuid, name, tgid, domainAffinityMap] = ret[domain.uuid];
        vmUuid = domain.uuid;
        name = domain.name;
        tgid = domain.tgid;
        if (domainAffinityMap.find(domain.numaId) == domainAffinityMap.end()) {
            domainAffinityMap[domain.numaId] = VmDomainAffinity{};
        }
        auto &[numaId, groups, vcpuAffinityInfoMap] = domainAffinityMap[domain.numaId];
        numaId = domain.numaId;
        groups = std::list<VmGroup>{};
        for (auto &groupId : domain.groups) {
            groups.emplace_back(groupMap_[groupId]);
        }
        GetVcpuAffinityInfoMap(domain, groups, vcpuAffinityInfoMap);
    }
}

void ClusterSched::GetVcpuAffinityInfoMap(VmDomain &domain, const std::list<VmGroup> &groups,
                                          std::map<uint16_t, VcpuAffinityInfo> &vcpuAffinityInfoMap)
{
    vcpuAffinityInfoMap = std::map<uint16_t, VcpuAffinityInfo>{};
    for (auto &group : groups) {
        for (auto &pid : group.entityPids) {
            auto vcpuId = domain.pidVcpuMap[pid];
            vcpuAffinityInfoMap[vcpuId] = VcpuAffinityInfo{
                .vcpu = vcpuId,
                .pid = pid,
                .cpuIdx = entityMap_.find(pid) != entityMap_.end() ?
                              numaClusterMap_[domain.numaId][group.clusterId].GetStartCpu() + entityMap_[pid].cpuIdx :
                              -1,
            };
        }
    }
}

/**
 * cpu rescheduling by vm uuid
 * @param value
 * @return VasRet
 */
VasRet ClusterSched::ReSchedVm(const std::string &value)
{
    LOG_INFO("Rescheduling VM vCPU affinity start.");
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock dataLock(dataMutex_);

    bool isExist = false;
    VasRet ret = VAS_OK;
    std::string uuid;
    for (auto &[domainKey, domain] : domainMap_) {
        if (value != "all" && value != domain.name && value != domain.uuid) {
            continue;
        }
        uuid = domain.uuid;
        isExist = true;
        Unassign(domain);
        domain.isReScheded = false;
        Free(domain);
        auto tmpRet = Alloc(domain);
        if (isVasRetFail(tmpRet)) {
            ret = VAS_ERROR;
            continue;
        }
        tmpRet = Assign(domain);
        domain.isReScheded = isVasRetOk(tmpRet);
        if (isVasRetFail(tmpRet)) {
            ret = VAS_ERROR;
        }
        domain.entityPids.clear(); // compaction could reassign tgid
    }
    if (!isExist) {
        LOG_ERROR("The virtual machine that needs to be reallocated does not exist.");
        return VAS_ERROR_INVAL;
    }
    std::vector<VmDomain> domains = ClusterSched::GetInstance().GetDomainsByUuid(uuid);
    ClusterCompactionWithoutLock();
    LOG_INFO("Rescheduling VM vCPU affinity end.");
    return ret;
}

void ClusterSched::ClusterCompactionWithoutLock()
{
    LOG_DEBUG("Start to compact cpu.");
    const auto start = high_resolution_clock::now();
    CleanDyingPid();
    for (auto &[numaId, clusterMap] : numaClusterMap_) {
        LOG_DEBUG("Start to compact cpu by numa, numa=" + std::to_string(numaId));
        CompactionCluster(numaId, clusterMap);
        LOG_DEBUG("End to compact cpu by numa, numa=" + std::to_string(numaId));
    }
    const auto duration = duration_cast<microseconds>(high_resolution_clock::now() - start);
    compactionCount_ += 1;

    LOG_DEBUG("End to compact cpu, total count=" + std::to_string(compactionCount_) +
              ", cost=" + std::to_string(duration.count()) + "us.");
}

/**
 * Update the collected VM information to the domainMap.
 * @param vmInfoMap
 */
void ClusterSched::UpdateDomainInfosWithoutLock(const VmInfoMap &vmInfoMap)
{
    auto numaUsedCpuMap = GetNumaUsedCpuMap();
    for (auto &[uuid, vmInfo] : vmInfoMap) {
        // Only the domain information is updated. The scheduling process is not concerned.
        VmDomain _{};
        // VmDomain about VMs that fail to be updated is skipped.
        UpdateDomainInfoWithoutLock(vmInfo, numaUsedCpuMap, _);
    }
}

/**
 * Update the collected VM information to the domainMap.
 * @param vmInfo vm info
 * @param numaUsedCpuMap Used CPU mapping table on the NUMA at all layers
 * @param vmDomain Specifies the VM info mapping object generated after NUMA is selected.
 * @return VasRet VM_OK: success others: failed
 */
VasRet ClusterSched::UpdateDomainInfoWithoutLock(const VmInfo &vmInfo, NumaUsedCpuMap &numaUsedCpuMap,
                                                 VmDomain &vmDomain)
{
    uint16_t numaId{};
    if (const auto ret = SelectVmNuma(vmInfo, numaUsedCpuMap, numaId); isVasRetFail(ret)) {
        LOG_WARN("No suitable numa for vm. uuid=" + vmInfo.uuid);
        return VAS_WARN;
    }
    LOG_DEBUG("vm name=" + vmInfo.name + ", uuid=" + vmInfo.uuid + ", select numa id=" + std::to_string(numaId));
    const std::string domainKey = vmInfo.uuid + "_" + std::to_string(numaId);
    if (domainMap_.find(domainKey) == domainMap_.end()) {
        PidVcpuMap pidVcpuMap{};
        Vcpu2CpuMap vcpu2CpuMap{};
        for (auto &[vcpu, vcpuInfo] : vmInfo.vcpuMap) {
            pidVcpuMap[vcpuInfo.pid] = vcpu;
            vcpu2CpuMap[vcpu] = vcpuInfo.cpuMaps;
        }
        std::set<pid_t> ioThreadIds{};
        for (auto &[ioThread, ioThreadInfo] : vmInfo.ioThreadMap) {
            ioThreadIds.emplace(ioThread);
        }
        domainMap_[domainKey] = VmDomain{
            .uuid = vmInfo.uuid,
            .name = vmInfo.name,
            .numaId = numaId,
            .tgid = vmInfo.tgid,
            .pidVcpuMap = pidVcpuMap,
            .commonCpuMap = VmDomain::GetCommonCpuMap(vcpu2CpuMap),
        };
    }
    vmDomain = domainMap_[domainKey];
    return VAS_OK;
}

uint16_t ClusterSched::GetNumaCpuCount(const uint16_t &numaId)
{
    uint16_t ret = 0;
    for (auto &[clusterId, cluster] : numaClusterMap_[numaId]) {
        ret += cluster.clusterLayers[0].total; // Every layer cpu count is same.
    }
    return ret;
}

NumaUsedCpuMap ClusterSched::GetNumaUsedCpuMap()
{
    auto accumulateUsedCpu = [](const std::map<uint16_t, Cluster> &clusterMap, const uint16_t &numaId,
                                const uint16_t &layerId,
                                std::map<uint16_t, std::map<std::uint16_t, uint16_t>> &ret) -> void {
        for (auto &[clusterId, cluster] : clusterMap) {
            if (ret[layerId].find(numaId) == ret[layerId].end()) {
                ret[layerId][numaId] = cluster.clusterLayers[layerId].total - cluster.clusterLayers[layerId].idle;
            } else {
                ret[layerId][numaId] += cluster.clusterLayers[layerId].total - cluster.clusterLayers[layerId].idle;
            }
        }
    };

    NumaUsedCpuMap ret{};
    for (auto &[numaId, clusterMap] : numaClusterMap_) {
        for (size_t layerId = 0; layerId < overProvision_[numaId]; ++layerId) {
            accumulateUsedCpu(clusterMap, numaId, layerId, ret);
        }
    }
    return ret;
}

VasRet ClusterSched::SelectMinLayer(const std::set<uint16_t> &availableNumas, uint16_t &selectNumaId)
{
    size_t minOverProvision = maxOverProvision;
    for (auto &numaId : availableNumas) {
        if (overProvision_[numaId] < minOverProvision) {
            minOverProvision = overProvision_[numaId];
            selectNumaId = numaId;
        }
    }
    if (minOverProvision == maxOverProvision) {
        return VAS_ERROR;
    }
    return VAS_OK;
}

VasRet ClusterSched::SelectVmNuma(const VmInfo &vmInfo, NumaUsedCpuMap &numaUsedCpuMap, uint16_t &selectNumaId)
{
    std::set<uint16_t> availableNumas{};
    for (const auto &[numaId, _] : numaClusterMap_) {
        availableNumas.emplace(numaId);
    }
    // Obtains the intersection of numaset
    for (auto &[vcpu, vcpuInfo] : vmInfo.vcpuMap) {
        if (availableNumas.empty()) {
            availableNumas = vcpuInfo.numaSet;
            continue;
        }
        std::set<uint16_t> intersection;
        std::set_intersection(availableNumas.begin(), availableNumas.end(), vcpuInfo.numaSet.begin(),
                              vcpuInfo.numaSet.end(), std::inserter(intersection, intersection.begin()));
        availableNumas = std::move(intersection);
    }
    if (availableNumas.empty()) {
        return VAS_ERROR;
    }
    LOG_DEBUG("availableNumas=" + StringUtil::SetToStr(availableNumas));
    // Select a numa with as full of CPUs as possible, provided that the numa has enough free CPUs
    int16_t selectNumaUsedCpuCount = -1;
    uint16_t selectLayerId = maxOverProvision;
    size_t layerId = 0;
    for (auto &numaId : availableNumas) {
        for (layerId = 0; layerId < overProvision_[numaId]; ++layerId) {
            uint16_t numaCpuCount = GetNumaCpuCount(numaId);
            LOG_DEBUG("layerId=" + std::to_string(layerId) + ", numaId=" + std::to_string(numaId) +
                      ", usedCpu=" + std::to_string(numaUsedCpuMap[layerId][numaId]) + ", cpus=" +
                      std::to_string(vmInfo.vcpuMap.size()) + ", numaCpuCount=" + std::to_string(numaCpuCount));
            if (selectNumaUsedCpuCount < numaUsedCpuMap[layerId][numaId] && selectLayerId >= layerId &&
                numaUsedCpuMap[layerId][numaId] + vmInfo.vcpuMap.size() <= numaCpuCount) {
                selectNumaId = numaId;
                selectLayerId = layerId;
                selectNumaUsedCpuCount = static_cast<int16_t>(numaUsedCpuMap[layerId][numaId]);
            }
        }
    }
    if (selectNumaUsedCpuCount != -1) {
        // Add the pre-allocated CPU to numaUsedCpuMap.
        numaUsedCpuMap[selectLayerId][selectNumaId] += vmInfo.vcpuMap.size();
        return VAS_OK;
    }
    if (isVasRetOk(SelectMinLayer(availableNumas, selectNumaId))) {
        return VAS_OK;
    }
    return VAS_ERROR;
}

/**
 * Reschedule vm which is already started before service
 */
void ClusterSched::ReSchedStartedVms()
{
    VmInfoMap vmInfoMap{};
    if (const auto ret = LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap); ret != VAS_OK) {
        LOG_WARN("Acquire vm info list failed.");
        return;
    }
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock lock(dataMutex_);
    UpdateDomainInfosWithoutLock(vmInfoMap);
    ReSchedStartedVmsWithoutLock();
}

void ClusterSched::ReSetSchedPolicy()
{
    std::lock_guard transactionLock(transactionMutex_);
    std::unique_lock dataLock(dataMutex_);

    VmInfoMap vmInfoMap{};
    LibvirtHelper::GetCache(vmInfoMap);
    RecoverVmVcpu_(vmInfoMap);
    RestoreVmInfoWithoutLock();
    ReSchedStartedVmsWithoutLock();
}

/**
 * Restore the VM vCPU binding relationship when the process exits.
 *
 */
void ClusterSched::RecoverVmVcpu(const VmInfoMap &vmInfoMap)
{
    std::lock_guard transactionLock(transactionMutex_);
    LOG_INFO("Restore the VM vCPU binding relationship start.");
    RecoverVmVcpu_(vmInfoMap);
    LOG_INFO("Restore the VM vCPU binding relationship end.");
}

void ClusterSched::RestoreVmInfoWithoutLock()
{
    CpuTopologyMap cpuTopologyMap = CpuHelper::GetInstance().GetCpuTopology();
    for (auto &[numaId, clusterMap] : numaClusterMap_) {
        for (auto &[clusterId, cluster] : clusterMap) {
            cluster.clusterLayers.clear();
            ClusterInfo &clusterInfo = cpuTopologyMap[clusterId];
            cluster.clusterLayers.emplace_back(ClusterLayer{
                .idle = static_cast<uint16_t>(std::count(clusterInfo.bitMap.begin(), clusterInfo.bitMap.end(), false)),
                .total = clusterInfo.total,
                .usedBitmap = clusterInfo.bitMap,
                .groups = std::set<std::string>{},
            });
        }

        overProvision_[numaId] = 1;
    }
    for (auto &[domainKey, domain] : domainMap_) {
        domain.groups.clear();
        domain.entityPids.clear();
        domain.isReScheded = false;
    }
    entityMap_.clear();
    groupMap_.clear();
}

void ClusterSched::ReSchedStartedVmsWithoutLock()
{
    for (auto &[domainKey, domain] : domainMap_) {
        auto ret = Alloc(domain);
        if (isVasRetFail(ret)) {
            LOG_WARN("Alloc domain failed. uuid=" + domain.uuid);
            continue;
        }
        ret = Assign(domain);
        domain.isReScheded = isVasRetOk(ret);
        if (isVasRetFail(ret)) {
            LOG_WARN("Assign domain failed. uuid=" + domain.uuid);
        }
    }
}

/**
 * alloc cpu for vm domain
 * @param domain VmDomain
 * @return VasRet
 */
VasRet ClusterSched::Alloc(VmDomain &domain)
{
    const auto granularity = GetGranularity();
    if (granularity == 0) {
        LOG_ERROR("Get granularity failed.");
        return VAS_ERROR;
    }
    uint16_t nr = domain.pidVcpuMap.size() * granularity;
    if (domain.isReScheded) {
        LOG_DEBUG("domain=" + domain.ToStr() + " already allocated.");
        return VAS_OK;
    }

    if (const uint16_t total = GetNumaTotalCpus(domain.numaId); nr > total) {
        LOG_ERROR("domain nr=" + std::to_string(nr) + " over numa cpu. numa=" + std::to_string(domain.numaId) +
                  ", total=" + std::to_string(total));
        return VAS_ERROR;
    }
    while (nr) {
        // allocate CPUs based on the cluster granularity
        if (const auto allocNum = AllocClusterGroupToDomain(domain, nr); isIntEqZero(allocNum)) {
            LOG_ERROR("Allocate domain=" + domain.ToStr() +
                      " failed, no enough cpu, numa=" + std::to_string(domain.numaId));
            return VAS_ERROR;
        } else {
            nr -= allocNum;
        }
    }

    LOG_INFO("Allocate domain=" + domain.ToStr() + " successfully, group=" + StringUtil::SetToStr(domain.groups));
    return VAS_OK;
}

/**
 * free vmDomain and vmGroup
 * @param domain
 * @return
 */
void ClusterSched::Free(VmDomain &domain)
{
    for (const auto &groupId : domain.groups) {
        if (groupMap_.find(groupId) == groupMap_.end()) {
            continue;
        }
        const auto &group = groupMap_[groupId];
        if (numaClusterMap_.find(domain.numaId) == numaClusterMap_.end() ||
            numaClusterMap_[domain.numaId].find(group.clusterId) == numaClusterMap_[domain.numaId].end() ||
            group.layerId >= numaClusterMap_[domain.numaId][group.clusterId].clusterLayers.size()) {
            groupMap_.erase(groupId);
            continue;
        }
        auto &clusterLayer = numaClusterMap_[domain.numaId][group.clusterId].clusterLayers[group.layerId];
        DelGroupFromClusterLayer(group, clusterLayer);
        groupMap_.erase(groupId);
    }
    domain.groups.clear();
}

/**
 * allocate CPUs to all vCPU threads of vm
 * @param domain VmDomain
 * @return VasRet
 */
VasRet ClusterSched::Assign(VmDomain &domain)
{
    VasRet ret = VAS_OK;
    for (auto &[pid, vCpuId] : domain.pidVcpuMap) {
        if (entityMap_.find(pid) != entityMap_.end()) {
            LOG_DEBUG("Pid is already assign. pid=" + std::to_string(pid));
            continue;
        }
        if (const auto tmpRet = AssignPidCpu(domain, pid); isVasRetFail(tmpRet)) {
            LOG_ERROR("Assign failed. pid=" + std::to_string(pid) + ", " + formatRetCode(tmpRet));
            ret = VAS_ERROR;
        }
    }
    if (isVasRetFail(ret)) {
        LOG_ERROR("Assign failed. uuid=" + domain.uuid + ", numaId=" + std::to_string(domain.numaId));
    } else {
        LOG_INFO("Assign successfully, uuid=" + domain.uuid + ", numaId=" + std::to_string(domain.numaId));
    }
    return ret;
}

/**
 * unassign vCPU
 * @param domain VmDomain
 */
void ClusterSched::Unassign(VmDomain &domain)
{
    UnAssignPidCpu(domain);
}

/**
 * bind vcpu to cpu
 * @param domain
 * @param pid
 * @param cpuIndex
 * @param clusterId
 * @return
 */
VasRet ClusterSched::SetVcpuAffinity(VmDomain &domain, const pid_t &pid, const uint16_t &cpuIndex,
                                     const uint16_t &clusterId)
{
    fs::path vmPathPre{};
    auto ret = GetVmCgroupPath(domain.uuid, vmPathPre);
    if (isVasRetFail(ret)) {
        return ret;
    }

    if (VasdArgParse::schedPolicy == "dynamicAffinity") {
        auto numaCpusetMap = CpuHelper::GetInstance().GetNuma2CpusetMap();
        const auto numaCpuBitMap =
            Bitset::GenDynamicBitsetByCpuSet(CpuHelper::MAX_CPU_NUM, numaCpusetMap[domain.numaId]);
        ret = SetVmCpuset(vmPathPre, VmThreadType::VCPU_CPUSET, numaCpuBitMap, domain.pidVcpuMap[pid]);
        if (isVasRetFail(ret)) {
            return VAS_ERROR;
        }
        if (numaClusterMap_.find(domain.numaId) == numaClusterMap_.end() ||
            numaClusterMap_[domain.numaId].find(clusterId) == numaClusterMap_[domain.numaId].end()) {
            LOG_ERROR("Invalid numaId or clusterId");
            return VAS_ERROR;
        }
        return SetVmCpuset(vmPathPre, VmThreadType::VCPU_PREFERRED_CPU,
                           Bitset::GenDynamicBitSetByArea(CpuHelper::MAX_CPU_NUM, cpuIndex, 1), domain.pidVcpuMap[pid]);
    }
    return SetVmCpuset(vmPathPre, VmThreadType::VCPU_CPUSET,
                       Bitset::GenDynamicBitSetByArea(CpuHelper::MAX_CPU_NUM, cpuIndex, 1), domain.pidVcpuMap[pid]);
}

/**
 * cpu rescheduling by VmDomain
 * @param domains VmDomain
 * @return VasRet
 */
VasRet ClusterSched::ReSched(std::vector<VmDomain> &domains)
{
    for (auto &domain : domains) {
        auto ret = Alloc(domain);
        if (isVasRetFail(ret)) {
            return ret;
        }
        ret = Assign(domain);
        domain.isReScheded = isVasRetOk(ret);
        domainMap_[domain.uuid + "_" + std::to_string(domain.numaId)] = domain;
    }
    return VAS_OK;
}

/**
 * cpu de-rescheduling
 * @param domains VmDomain
 * @return VasRet
 */
VasRet ClusterSched::DeReSched(std::vector<VmDomain> &domains)
{
    for (auto &domain : domains) {
        Unassign(domain);
        domain.isReScheded = false;
        Free(domain);
        domainMap_.erase(domain.uuid + "_" + std::to_string(domain.numaId));
    }
    return VAS_OK;
}

/**
 * get the hyper-threading count from the first CPU in the first NUMA node.
 * @return uint16_t the hyper-threading count
 */
uint16_t ClusterSched::GetGranularity()
{
    if (VasdArgParse::smt) {
        return 1;
    }
    return CpuHelper::GetInstance().GetSmtCpuNr();
}

/**
 * 1:1 core binding for cpu allocation
 * @param pid
 * @param domain
 * @return VasRet
 */
VasRet ClusterSched::AssignPidCpu(VmDomain &domain, const pid_t &pid)
{
    for (auto &groupId : domain.groups) {
        auto &group = groupMap_[groupId];
        const auto index = Bitset::FindFirstIdlePos(group.usedBitmap, group.start, GetGranularity());
        if (isIntInvalid(index) || index > group.start + group.nrCpus - 1) {
            continue;
        }
        if (numaClusterMap_.find(domain.numaId) == numaClusterMap_.end() ||
            numaClusterMap_[domain.numaId].find(group.clusterId) == numaClusterMap_[domain.numaId].end()) {
            continue;
        }
        const auto cluster = numaClusterMap_[domain.numaId][group.clusterId];
        const auto cpuIndex = cluster.GetStartCpu() + index;
        if (const auto ret = SetVcpuAffinity(domain, pid, cpuIndex, group.clusterId); isVasRetFail(ret)) {
            return VAS_ERROR;
        }
        const VmEntity &entity = GenEntity(pid, index);
        AddEntityToGroup(entity, GetGranularity(), group);
        return VAS_OK;
    }
    return VAS_ERROR;
}

/**
 * unassign 1:1 core binding
 * @param domain VmDomain
 */
void ClusterSched::UnAssignPidCpu(const VmDomain &domain)
{
    for (auto &groupId : domain.groups) {
        if (groupMap_.find(groupId) == groupMap_.end()) {
            continue;
        }
        auto &group = groupMap_[groupId];
        for (auto &pid : group.entityPids) {
            if (entityMap_.find(pid) == entityMap_.end()) {
                continue;
            }
            auto &entity = entityMap_[pid];
            Bitset::DynamicBitsetClear(group.usedBitmap, entity.cpuIdx, 1);
            entityMap_.erase(pid);
        }
        group.entityPids.clear();
    }
}

void ClusterSched::RecoverVmVcpu_(const VmInfoMap &vmInfoMap)
{
    LOG_INFO("Start recover vm affinity setting.");
    for (auto &[uuid, vmInfo] : vmInfoMap) {
        LOG_DEBUG("Recover vm; uuid=" + uuid + ", vcpu=" + std::to_string(vmInfo.vcpuMap.size()) +
                  ", ioThreadMap=" + std::to_string(vmInfo.ioThreadMap.size()));
        fs::path vmPathPre{};
        if (const auto ret = GetVmCgroupPath(vmInfo.uuid, vmPathPre); isVasRetFail(ret)) {
            LOG_WARN("Get vm cgroup path failed.");
            continue;
        }
        const DynamicBitset emptyDynamicBitset(CpuHelper::MAX_CPU_NUM, false);
        for (auto &[vcpuId, vcpuInfo] : vmInfo.vcpuMap) {
            SetVmCpuset(vmPathPre, VmThreadType::VCPU_CPUSET, vcpuInfo.cpuMaps, vcpuId);
            SetVmCpuset(vmPathPre, VmThreadType::VCPU_PREFERRED_CPU, emptyDynamicBitset, vcpuId);
        }
    }
    LOG_INFO("Success to recover vm affinity setting.");
}

/**
 * get numa total cpu count
 * @param numaId
 * @return uint16_t numa total cpu count
 */
uint16_t ClusterSched::GetNumaTotalCpus(const uint16_t &numaId)
{
    uint16_t ret = 0;
    if (numaClusterMap_.find(numaId) == numaClusterMap_.end()) {
        return ret;
    }
    for (const auto &[clusterId, cluster] : numaClusterMap_[numaId]) {
        if (cluster.clusterLayers.empty()) {
            continue;
        }
        ret += cluster.clusterLayers[0].total * cluster.clusterLayers.size();
    }
    return ret;
}

/**
 * alloc cpu for vm domain at the cluster granularity
 * 1. If nr exceeds the total number of CPUs in a Cluster, directly alloc a completely idle Cluster.
 * 2. If nr does not exceed the total number of CPUs in a Cluster, alloc nr CPUs from a Cluster
 * where the number of idle CPUs is greater than or equal to nr.
 * @param domain
 * @param nr
 * @return uint16_t allocated num
 */
uint16_t ClusterSched::AllocClusterGroupToDomain(VmDomain &domain, const uint16_t &nr)
{
    uint16_t allocNum = 0;
    auto &clusterList = numaClusterMap_[domain.numaId];
    for (auto layerId = 0; layerId < overProvision_[domain.numaId]; ++layerId) {
        for (auto &[clusterId, cluster] : clusterList) {
            if (cluster.clusterLayers.empty() || layerId >= cluster.clusterLayers.size()) {
                continue;
            }
            auto availableCpuMap = Bitset::DynamicBitsetNot(
                Bitset::DynamicBitsetCut(domain.commonCpuMap, cluster.GetStartCpu(), cluster.cpuSet.size()));
            allocNum = AllocGroupFromCluster(nr, layerId, domain, cluster, availableCpuMap);
            if (allocNum == 0) {
                continue;
            }
            LOG_INFO("Allocated clusterId=" + std::to_string(cluster.id) + ", allocNum=" + std::to_string(allocNum) +
                     ", layerId=" + std::to_string(layerId));
            break;
        }
        if (allocNum != 0) {
            break;
        }
        if (layerId == overProvision_[domain.numaId] - 1 && overProvision_[domain.numaId] < maxOverProvision &&
            VasdArgParse::schedPolicy == SCHED_POLICY_DYNAMIC) {
            OverProvisionUp(domain.numaId);
        } else {
            LOG_WARN("overProvision=" + std::to_string(maxOverProvision) + ", can't raise up again.");
        }
    }
    return allocNum;
}

/**
 * generate vCPU thread allocation object
 * @param pid
 * @param pinType
 * @param index default value: -1
 * @return VmEntity object
 */
VmEntity &ClusterSched::GenEntity(const pid_t &pid, const int16_t &index)
{
    const VmEntity entity{
        .pid = pid,
        .cpuIdx = index,
    };
    entityMap_[pid] = entity;
    return entityMap_[pid];
}

/**
 * bind Entity to Group, refresh usedBitset
 * @param entity
 * @param group
 * @param granularity
 */
void ClusterSched::AddEntityToGroup(const VmEntity &entity, const uint16_t &granularity, VmGroup &group)
{
    Bitset::DynamicBitsetSet(group.usedBitmap, entity.cpuIdx, granularity);
    group.entityPids.emplace(entity.pid);
}

/**
 * Allocate a new group and bind it to the domain and cluster.
 * @param nr
 * @param layerId
 * @param domain
 * @param cluster
 * @param availableCpuMap
 * @return uint16_t allocated num
 */
uint16_t ClusterSched::AllocGroupFromCluster(const uint16_t &nr, const uint8_t &layerId, VmDomain &domain,
                                             Cluster &cluster, DynamicBitset &availableCpuMap)
{
    auto &[free, total, usedBitmap, groups] = cluster.clusterLayers[layerId];
    uint16_t allocNum;
    int16_t start = -1;
    Bitset::DynamicBitsetOr(availableCpuMap, cluster.clusterLayers[layerId].usedBitmap);
    if (nr >= total && total == free) {
        start = Bitset::FindFirstIdlePos(availableCpuMap, 0, total);
        allocNum = total;
    } else if (free >= nr) {
        start = Bitset::FindFirstIdlePos(availableCpuMap, 0, nr);
        allocNum = nr;
    }
    if (isIntInvalid(start)) {
        LOG_WARN("Not enough consecutive free CPUs found.");
        return 0;
    }
    const VmGroup group{
        .domainKey = domain.uuid + "_" + std::to_string(domain.numaId),
        .id = domain.uuid + "_" + std::to_string(domain.numaId) + "_" + std::to_string(cluster.id) + "_" +
              std::to_string(layerId),
        .clusterId = cluster.id,
        .layerId = layerId,
        .start = start,
        .nrCpus = allocNum,
        .usedBitmap = DynamicBitset(CpuHelper::CLUSTER_CPU_NUM, false),
    };
    groupMap_[group.id] = group;
    // bind group to cluster layer
    AddGroupToClusterLayer(group, cluster.clusterLayers[layerId]);
    // bind group to domain
    AddGroupToDomain(group, domain);
    return allocNum;
}

/**
 * bind group to cluster layer
 * @param group
 * @param clusterLayer
 */
void ClusterSched::AddGroupToClusterLayer(const VmGroup &group, ClusterLayer &clusterLayer)
{
    clusterLayer.idle -= group.nrCpus;
    clusterLayer.groups.emplace(group.id);
    Bitset::DynamicBitsetSet(clusterLayer.usedBitmap, group.start, group.nrCpus);
}

/**
 * delete group from cluster layer
 * @param group
 * @param clusterLayer
 */
void ClusterSched::DelGroupFromClusterLayer(const VmGroup &group, ClusterLayer &clusterLayer)
{
    Bitset::DynamicBitsetClear(clusterLayer.usedBitmap, group.start, group.nrCpus);
    clusterLayer.idle += group.nrCpus;
    clusterLayer.groups.erase(group.id);
}

/**
 * bind group to domain
 * @param group
 * @param domain
 */
void ClusterSched::AddGroupToDomain(const VmGroup &group, VmDomain &domain)
{
    domain.groups.emplace(group.id);
}

/**
 * delete group from domain
 * @param group
 * @param domain
 */
void ClusterSched::DelGroupFromDomain(const VmGroup &group, VmDomain &domain)
{
    domain.groups.erase(group.id);
}

/**
 * calculate the CPU mask for all groups within the vm domains.
 * @param domain
 * @return
 */
DynamicBitset ClusterSched::GetDomainCpuMask(const std::vector<VmDomain> &domains)
{
    DynamicBitset cpuMask(CpuHelper::MAX_CPU_NUM, false);
    for (const auto &domain : domains) {
        for (auto &groupId : domain.groups) {
            const auto [domainKey, id, clusterId, layerId, start, nrCpus, usedBitmap, entityPids] = groupMap_[groupId];
            if (numaClusterMap_.find(domain.numaId) == numaClusterMap_.end() ||
                numaClusterMap_[domain.numaId].find(clusterId) == numaClusterMap_[domain.numaId].end()) {
                continue;
            }
            const auto cluster = numaClusterMap_[domain.numaId][clusterId];
            const auto startCpu = cluster.GetStartCpu();
            const auto usedCpuMask = Bitset::GenDynamicBitSetByArea(CpuHelper::MAX_CPU_NUM, startCpu + start, nrCpus);
            Bitset::DynamicBitsetOr(cpuMask, usedCpuMask);
        }
    }
    return cpuMask;
}

/**
 * get vm cgroup path
 * @param uuid
 * @param cpuPath
 * @return VasRet
 */
VasRet ClusterSched::GetVmCgroupPath(const std::string &uuid, fs::path &cpuPath)
{
    virDomainPtr domainConn = nullptr;
    if (const auto ret = LibvirtHelper::GetInstance().GetDomainConnByUUID(uuid, domainConn);
        isVasRetFail(ret) || !domainConn) {
        LOG_ERROR("Get domain connect by uuid failed. uuid=" + uuid);
        LibvirtHelper::FreeDomain(domainConn);
        return ret;
    }
    uint16_t vmId{};
    if (const auto ret = LibvirtHelper::GetVmID(domainConn, vmId); isVasRetFail(ret)) {
        LOG_ERROR("Get domain id by uuid failed. uuid=" + uuid);
        LibvirtHelper::FreeDomain(domainConn);
        return ret;
    }
    LibvirtHelper::FreeDomain(domainConn);
    if (!exists(vmCgroupPrefix) || !is_directory(vmCgroupPrefix)) {
        LOG_ERROR("Machine cgroup path is not exist. uuid=" + uuid);
        return VAS_ERROR;
    }
    std::ostringstream oss;
    oss << R"(machine-qemu\\x2d)" << vmId << R"(\\x2d.*)";
    const std::regex machinePathP(oss.str());
    for (auto &dir : fs::directory_iterator(vmCgroupPrefix)) {
        std::string dirName(dir.path().filename().c_str());
        if (std::smatch match; std::regex_search(dirName, match, machinePathP)) {
            cpuPath = vmCgroupPrefix / match[0].str() / "libvirt";
            return VAS_OK;
        }
    }
    LOG_ERROR("Domain cgroup path is not exist. uuid=" + uuid);
    return VAS_ERROR;
}

/**
 * set vm cpu bitset (bind vcpu to cpu)
 * @param vmPathPre
 * @param threadType
 * @param cpuBitSet
 * @param id
 * @return VasRet
 */
VasRet ClusterSched::SetVmCpuset(const fs::path &vmPathPre, const VmThreadType &threadType,
                                 const DynamicBitset &cpuBitSet, const int32_t &id)
{
    fs::path modifyPath{};
    switch (threadType) {
        case VmThreadType::VCPU_CPUSET:
            modifyPath = vmPathPre / ("vcpu" + std::to_string(id)) / "cpuset.cpus";
            break;
        case VmThreadType::VCPU_PREFERRED_CPU:
            modifyPath = vmPathPre / ("vcpu" + std::to_string(id)) / "cpuset.preferred_cpus";
            break;
        default:
            LOG_ERROR("Invalid vm thread type.");
            return VAS_ERROR;
    }

    std::vector<__u32> caps = {CAP_DAC_OVERRIDE};
    if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_ADD) != VAS_OK) {
        LOG_ERROR("Add capabilities failed.");
        return VAS_ERROR;
    }

    std::ofstream file(modifyPath);
    if (!file.is_open()) {
        LOG_ERROR("Open file failed.");
        VasSecurityManager::ClearCapabilities(caps);
        return VAS_ERROR;
    }
    file << Bitset::GetCpuSetFromDynamicBitset(cpuBitSet);
    if (file.fail()) {
        LOG_ERROR("Set vm cpuset failed. path=" + modifyPath.string());
        VasSecurityManager::ClearCapabilities(caps);
        return VAS_ERROR;
    }
    LOG_DEBUG("Set vm cpuset success. path=" + modifyPath.string());

    if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_DELETE) != VAS_OK) {
        LOG_ERROR("Delete capabilities failed.");
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * clean non-existent PIDs
 * @return VasRet
 */
VasRet ClusterSched::CleanDyingPid()
{
    LOG_DEBUG("Start to clean entity which pid is dying.");
    Uuid2PidMap uuid2PidMap = ProcHelper::GetInstance().GetUuid2PidMap();
    for (auto domainIt = domainMap_.begin(); domainIt != domainMap_.end();) {
        auto &domain = domainIt->second;
        CleanDyingPidByGroup(domain);
        if (uuid2PidMap.find(domain.uuid) == uuid2PidMap.end() && domain.groups.empty() && domain.entityPids.empty()) {
            domainIt = domainMap_.erase(domainIt);
        } else {
            ++domainIt;
        }
    }
    LOG_DEBUG("End to clean entity which pid is dying.");
    return VAS_OK;
}

/**
 * clean up entities with non-existent PIDs in 1:1 core binding.
 * @param domain
 */
void ClusterSched::CleanDyingPidByGroup(VmDomain &domain)
{
    std::set<pid_t> dyingPids{};
    for (auto &groupId : domain.groups) {
        if (groupMap_.find(groupId) == groupMap_.end()) {
            LOG_WARN("GroupId=" + groupId + " not exist in groupMap.");
            continue;
        }
        auto &group = groupMap_[groupId];
        if (numaClusterMap_.find(domain.numaId) == numaClusterMap_.end()) {
            LOG_WARN("numaId=" + std::to_string(domain.numaId) + " not exist in numaClusterMap.");
            continue;
        }
        if (numaClusterMap_[domain.numaId].find(group.clusterId) == numaClusterMap_[domain.numaId].end()) {
            LOG_WARN("ClusterId=" + std::to_string(group.clusterId) + " not exist in clusterMap.");
            continue;
        }
        auto &cluster = numaClusterMap_[domain.numaId][group.clusterId];
        for (auto pidIt = group.entityPids.begin(); pidIt != group.entityPids.end();) {
            auto pid = *pidIt;
            if (domain.pidVcpuMap.find(pid) != domain.pidVcpuMap.end()) {
                ++pidIt;
                continue;
            }
            // If the assigned process does not exist
            if (entityMap_.find(pid) == entityMap_.end()) {
                ++pidIt;
                continue;
            }

            const auto &entity = entityMap_[pid];
            Bitset::DynamicBitsetClear(group.usedBitmap, entity.cpuIdx, 1);
            entityMap_.erase(pid);
            dyingPids.emplace(pid);
            pidIt = group.entityPids.erase(pidIt);
        }
        for (auto &pid : dyingPids) {
            group.entityPids.erase(pid);
        }
        if (group.entityPids.empty()) {
            if (group.layerId < cluster.clusterLayers.size()) {
                DelGroupFromClusterLayer(group, cluster.clusterLayers[group.layerId]);
            }
            DelGroupFromDomain(group, domain);
        }
        dyingPids.clear();
    }
}

/**
 * compress cluster CPU usage for one numa
 * @param clusterMap
 * @return
 */
void ClusterSched::CompactionCluster(uint16_t numaId, std::map<uint16_t, Cluster> &clusterMap)
{
    for (auto layerId = 0; layerId < overProvision_[numaId]; ++layerId) {
        CompactionClusterOneLayer(clusterMap, layerId);
    }
}

void ClusterSched::CompactionClusterOneLayer(std::map<uint16_t, Cluster> &clusterMap, const uint8_t &layerId)
{
    for (auto clusterIt = clusterMap.begin(); clusterIt != clusterMap.end(); ++clusterIt) {
        // clusters that are completely empty or full do not participate in the compression process,
        // they will be merged after the process is completed.
        auto &cluster = clusterIt->second;
        // skip full used cluster.
        if (cluster.clusterLayers[layerId].idle == 0) {
            continue;
        }
        // full empty cluster need compaction from last layer.
        if (cluster.clusterLayers[layerId].idle != cluster.clusterLayers[layerId].total) {
            // compaction between cluster.
            if (auto nextClusterIt = std::next(clusterIt); nextClusterIt != clusterMap.end()) {
                CompactionGroupWithinCluster(cluster, nextClusterIt->second, layerId);
            }
        }
        // compaction from last layer.
        if (layerId != overProvision_[cluster.numaId] - 1) {
            CompactionGroupFromLastLayer(cluster, layerId);
        }
    }
}

/**
 * migrate vcpu to bind new cpu (change VmEntity cpuIdx) [No consideration of partial success logic]
 *
 * @param group the group need to update
 * @param newStart group start index
 * @param cluster cluster need to migrate to
 * @param layerId layerId need to migrate to
 * @return VasRet
 */
VasRet ClusterSched::GroupEntityMigrate(VmGroup &group, const int16_t &newStart, const Cluster &cluster,
                                        const uint8_t &layerId)
{
    std::vector<std::pair<pid_t, int>> cpuIdxBackup{};
    for (auto &pid : group.entityPids) {
        if (entityMap_.find(pid) == entityMap_.end()) {
            LOG_ERROR("Pid=" + std::to_string(pid) + " not exist in entity map.");
            return VAS_ERROR;
        }
        int oldCpuIdx = entityMap_[pid].cpuIdx;
        entityMap_[pid].cpuIdx = newStart + (entityMap_[pid].cpuIdx - group.start);
        const auto cpu = cluster.GetStartCpu() + entityMap_[pid].cpuIdx;
        if (const auto ret = SetVcpuAffinity(domainMap_[group.domainKey], pid, cpu, cluster.id); isVasRetFail(ret)) {
            // rollback changed cpuIdx
            for (const auto &[vcpuPid, cpuIdx] : cpuIdxBackup) {
                entityMap_[vcpuPid].cpuIdx = cpuIdx;
            }
            return ret;
        }
        // save current cpuIdx backup
        cpuIdxBackup.emplace_back(pid, oldCpuIdx);
    }
    group.id = group.domainKey + "_" + std::to_string(cluster.id) + "_" + std::to_string(layerId);
    group.clusterId = cluster.id;
    group.layerId = layerId;
    group.usedBitmap = Bitset::GenDynamicBitSetByArea(CpuHelper::CLUSTER_CPU_NUM, newStart, group.nrCpus);
    group.start = newStart;
    return VAS_OK;
}

void ClusterSched::OverProvisionUp(uint16_t numaId)
{
    CpuTopologyMap cpuTopologyMap = CpuHelper::GetInstance().GetCpuTopology();
    for (auto &[numaId, clusterMap] : numaClusterMap_) {
        for (auto &[clusterId, cluster] : clusterMap) {
            if (cluster.clusterLayers.empty()) {
                return;
            }
            ClusterInfo &clusterInfo = cpuTopologyMap[clusterId];
            cluster.clusterLayers.emplace_back(ClusterLayer{
                .idle = static_cast<uint16_t>(std::count(clusterInfo.bitMap.begin(), clusterInfo.bitMap.end(), false)),
                .total = clusterInfo.total,
                .usedBitmap = clusterInfo.bitMap,
                .groups = std::set<std::string>{},
            });
        }
    }
    ++overProvision_[numaId];
}

void ClusterSched::OverProvisionDown(uint16_t numaId)
{
    for (auto &[numaId, clusterMap] : numaClusterMap_) {
        for (auto &[clusterId, cluster] : clusterMap) {
            if (cluster.clusterLayers.empty()) {
                return;
            }
            cluster.clusterLayers.pop_back();
        }
    }
    --overProvision_[numaId];
}

/**
 * traverse group between neighboring cluster, migrate used group forward
 * @param cluster
 * @param nextCluster
 * @param layerId
 */
void ClusterSched::CompactionGroupWithinCluster(Cluster &cluster, Cluster &nextCluster, const uint8_t &layerId)
{
    auto &nextClusterLayer = nextCluster.clusterLayers[layerId];
    auto &clusterLayer = cluster.clusterLayers[layerId];
    for (auto groupIt = nextClusterLayer.groups.begin(); groupIt != nextClusterLayer.groups.end();) {
        auto oldGroupId = *groupIt;
        if (groupMap_.find(oldGroupId) == groupMap_.end()) {
            LOG_WARN("GroupId=" + oldGroupId + " not exist in groupMap.");
            continue;
        }
        auto group = groupMap_[oldGroupId];
        const auto nextGroupIt = std::next(groupIt);
        auto availableCpuMap = Bitset::DynamicBitsetNot(Bitset::DynamicBitsetCut(
            domainMap_[group.domainKey].commonCpuMap, cluster.GetStartCpu(), cluster.cpuSet.size()));
        Bitset::DynamicBitsetOr(availableCpuMap, cluster.clusterLayers[layerId].usedBitmap);
        const auto start = Bitset::FindFirstIdlePos(availableCpuMap, 0, group.nrCpus);
        if (isIntInvalid(start)) {
            ++groupIt;
            continue;
        }
        LOG_INFO("Start to allocate group from cluster (id=" + std::to_string(nextCluster.id) +
                 ") to neighbouring cluster (id=" + std::to_string(cluster.id) + "), groupId=" + group.id);
        DelGroupFromClusterLayer(group, nextClusterLayer);
        if (isVasRetFail(GroupEntityMigrate(group, start, cluster, layerId))) {
            LOG_WARN("Vm group migrate failed.");
            AddGroupToClusterLayer(group, nextClusterLayer);
            groupIt = nextGroupIt;
            continue;
        }
        // update groupMap and domainMap
        groupMap_.erase(oldGroupId);
        groupMap_[group.id] = group;
        domainMap_[group.domainKey].groups.erase(oldGroupId);
        domainMap_[group.domainKey].groups.insert(group.id);

        AddGroupToClusterLayer(group, clusterLayer);
        std::string uuid = domainMap_[group.domainKey].uuid;
        groupIt = nextGroupIt;
    }
}

/**
 * traverse group between current layer and last layer, migrate used group forward
 * @param cluster
 * @param layerId
 */
void ClusterSched::CompactionGroupFromLastLayer(Cluster &cluster, const uint8_t &layerId)
{
    uint16_t lastLayerEmptyClusterNum = 0;
    auto &clusterLayer = cluster.clusterLayers[layerId];
    for (auto &[lastClusterId, lastCluster] : numaClusterMap_[cluster.numaId]) {
        auto &lastClusterLayer = lastCluster.clusterLayers[overProvision_[cluster.numaId] - 1];
        if (lastClusterLayer.total == lastClusterLayer.idle) {
            ++lastLayerEmptyClusterNum;
            continue;
        }
        for (auto groupIt = lastClusterLayer.groups.begin(); groupIt != lastClusterLayer.groups.end();) {
            auto oldGroupId = *groupIt;
            auto group = groupMap_[oldGroupId];
            const auto nextGroupIt = std::next(groupIt);
            auto availableCpuMap = Bitset::DynamicBitsetNot(Bitset::DynamicBitsetCut(
                domainMap_[group.domainKey].commonCpuMap, cluster.GetStartCpu(), cluster.cpuSet.size()));
            Bitset::DynamicBitsetOr(availableCpuMap, cluster.clusterLayers[layerId].usedBitmap);
            const auto start = Bitset::FindFirstIdlePos(availableCpuMap, 0, group.nrCpus);
            if (isIntInvalid(start) || start > group.start) {
                ++groupIt;
                continue;
            }
            LOG_INFO("Start to allocate group from last cluster (id=" + std::to_string(lastCluster.id) +
                     ") to cluster (id=" + std::to_string(cluster.id) + "), groupId=" + group.id);
            DelGroupFromClusterLayer(group, lastClusterLayer);
            if (isVasRetFail(GroupEntityMigrate(group, start, cluster, layerId))) {
                LOG_WARN("Vm group migrate failed.");
                AddGroupToClusterLayer(group, lastClusterLayer);
                groupIt = nextGroupIt;
                continue;
            }
            groupMap_.erase(oldGroupId);
            groupMap_[group.id] = group;
            domainMap_[group.domainKey].groups.erase(oldGroupId);
            domainMap_[group.domainKey].groups.insert(group.id);
            AddGroupToClusterLayer(group, clusterLayer);
            std::string uuid = domainMap_[group.domainKey].uuid;
            groupIt = nextGroupIt;
        }
        if (lastClusterLayer.total == lastClusterLayer.idle) {
            ++lastLayerEmptyClusterNum;
        }
    }
    if (lastLayerEmptyClusterNum == numaClusterMap_[cluster.numaId].size()) {
        OverProvisionDown(cluster.numaId);
    }
}
} // namespace vas::sched
