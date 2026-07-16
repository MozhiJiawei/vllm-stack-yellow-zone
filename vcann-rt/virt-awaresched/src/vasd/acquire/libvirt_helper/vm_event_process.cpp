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

#include "vm_event_process.h"

#include <thread>

#include "conf.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "proc_helper.h"

namespace vas::sched {
using namespace vas::common;
using namespace vas::sched::acquire;
std::queue<VmEventInfo> VmEventProcess::vmEventQueue{};
std::mutex VmEventProcess::mtx{};
std::condition_variable VmEventProcess::eventCv{};

/**
 * run the vm monitor thread
 * @return
 */
void VmEventProcess::Run()
{
    auto eventThread = std::thread(&RunListener);
    eventThread.detach();
}

/**
 * stop the vm monitor thread
 * @return
 */
void VmEventProcess::Stop()
{
    std::lock_guard lock(mtx);
    eventCv.notify_all();
}

/**
 * Cyclicly monitor virtual machine events
 */
void VmEventProcess::RunListener()
{
    LOG_INFO("Start to listen vm event");
    if (const auto ret = LibvirtHelper::GetInstance().RunEventDefaultImpl(EventCallback); isVasRetFail(ret)) {
        LOG_ERROR("Failed to listen vm event");
    }
}

/**
 * VM event processing callback function
 * @param domain
 * @param event
 * @param detail
 * @return
 */
int VmEventProcess::EventCallback(virConnectPtr /* conn */, virDomainPtr domain, int event, int detail,
                                  void * /* opaque */)
{
    if (domain == nullptr) {
        LOG_ERROR("Domain ptr is nullptr, event callback handle failed.");
        return static_cast<int>(VAS_ERROR);
    }
    VmEventInfo vmEventInfo{
        .type = static_cast<virDomainEventType>(event),
        .detailType = static_cast<virDomainEventDetailType>(detail),
        .vmInfo = VmInfo{},
    };
    const auto findResult = VM_EVENT_MAP.find(std::pair(vmEventInfo.type, vmEventInfo.detailType));
    if (findResult == VM_EVENT_MAP.end()) {
        LOG_WARN("Untracked domain event. type=" + std::to_string(event) + ", detail=" + std::to_string(detail));
        return static_cast<int>(VAS_OK);
    }
    vmEventInfo.eventType = findResult->second;
    LOG_INFO("Receive domain event. id=" + std::to_string(static_cast<int>(vmEventInfo.eventType)));
    switch (findResult->second) {
        case VmEventType::START: {
            auto &vmInfo = vmEventInfo.vmInfo;
            if (const auto ret = LibvirtHelper::GetVmInfo(domain, vmInfo); isVasRetFail(ret)) {
                return static_cast<int>(ret);
            }
            auto vmVcpuPidList = ProcHelper::GetInstance().GetVmProcList(std::vector{vmInfo.uuid});
            auto uuid2PidMap = ProcHelper::GetInstance().GetUuid2PidMap();
            LibvirtHelper::FlushVmPidInfo(vmVcpuPidList, uuid2PidMap, vmInfo);
            LibvirtHelper::AddVmInfoToCache(vmInfo);
            Push(vmEventInfo);
            break;
        }
        case VmEventType::SHUTDOWN: {
            if (const auto ret = LibvirtHelper::GetVmUUID(domain, vmEventInfo.vmInfo.uuid); isVasRetFail(ret)) {
                return VAS_ERROR;
            }
            LibvirtHelper::DelVmInfoToCache(vmEventInfo.vmInfo.uuid);
            Push(vmEventInfo);
            break;
        }
        default: {
            LOG_WARN("Unknown domain event, skip.");
            break;
        }
    }
    return static_cast<int>(VAS_OK);
}

/**
 * push the vm event info
 * @param vmEventInfo
 */
void VmEventProcess::Push(const VmEventInfo &vmEventInfo)
{
    std::lock_guard lock(mtx);
    vmEventQueue.push(vmEventInfo);
    eventCv.notify_all();
}

/**
 * pop the vm event info
 * @return
 */
VasRet VmEventProcess::Pop(VmEventInfo &vmEventInfo)
{
    std::unique_lock lock(mtx);
    eventCv.wait(lock, [] { return !vmEventQueue.empty() || Conf::exitFlag.load(); });
    if (Conf::exitFlag.load()) {
        LOG_ERROR("Exit flag is true. Vm event queue remain " + std::to_string(vmEventQueue.size()) + ". Will ignore");
        return VAS_ERROR;
    }
    vmEventInfo = vmEventQueue.front();
    vmEventQueue.pop();
    return VAS_OK;
}
} // namespace vas::sched
