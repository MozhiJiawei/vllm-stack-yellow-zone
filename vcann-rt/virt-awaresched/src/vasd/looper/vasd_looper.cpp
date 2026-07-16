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

#include "vasd_looper.h"

#include "api.h"
#include "cluster_sched.h"
#include "conf.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "socket_server.h"
#include "vm_event_process.h"

namespace vas::sched {
SocketServer VasdLooper::server{};
std::thread VasdLooper::eventThread{};
std::thread VasdLooper::timerThread{};
constexpr int16_t THREAD_SLEEP = 100;

void VasdLooper::Run()
{
    // Listening to VM events
    VmEventProcess::Run();
    // Handling Virtual Machine Queue Events
    eventThread = std::thread(&VmEventHandler);
    // Periodically organize the CPU
    timerThread = std::thread(&ClusterCompactionTimer);
    // socketServer start
    StartSocketServer();
}

void VasdLooper::Stop()
{
    VmEventProcess::Stop();
    server.CloseServer();
    // join thread wait for exit
    eventThread.join();
    timerThread.join();
}

/**
 * Virtual machine event queue triggers CPU scheduling
 */
void VasdLooper::VmEventHandler()
{
    LOG_INFO("Start to listen vm event queue");
    while (!Conf::exitFlag.load()) {
        VmEventInfo vmEventInfo{};
        if (const auto ret = VmEventProcess::Pop(vmEventInfo); isVasRetFail(ret)) {
            break;
        }
        LOG_INFO("Vm event comming. eventType=" + std::to_string(static_cast<uint32_t>(vmEventInfo.eventType)));
        switch (vmEventInfo.eventType) {
            case VmEventType::START: {
                ClusterSched::GetInstance().AddDomainInfo(vmEventInfo.vmInfo);
                break;
            }
            case VmEventType::SHUTDOWN: {
                ClusterSched::GetInstance().DelDomainInfo(vmEventInfo.vmInfo.uuid);
                break;
            }
            default: {
                break;
            }
        }
        LOG_INFO("Vm event handler end. Wait next event.");
    }
    LOG_INFO("Exit flag is true. vm event handler exit.");
}

/**
 * Periodically perform cpu compaction
 */
void VasdLooper::ClusterCompactionTimer()
{
    LOG_INFO("Start to run cluster compaction looper");
    while (!Conf::exitFlag.load()) {
        LOG_INFO("Start compaction.");
        // Collecting Virtual Machine Information
        ClusterSched::GetInstance().UpdateDomainInfosAndSched();
        // Compact fragment
        ClusterSched::GetInstance().ClusterCompaction();
        std::this_thread::sleep_for(std::chrono::milliseconds(COMPACTION_INTERVAL));
    }
    VmInfoMap vmInfoMap{};
    LibvirtHelper::GetCache(vmInfoMap);
    ClusterSched::GetInstance().RecoverVmVcpu(vmInfoMap);
    LOG_INFO("Exit flag is true. Cluster compaction timer exit.");
}

void VasdLooper::StartSocketServer()
{
    try {
        if (!server.StartServer()) {
            LOG_ERROR("Failed to start server on uds.");
            return;
        }
        LOG_INFO("Server started. Waiting for client connection.");

        while (!Conf::exitFlag.load()) {
            if (!server.AcceptClient()) {
                LOG_ERROR("Failed to accept client connection");
                continue;
            }
            LOG_INFO("Client connected successfully");
            std::string receivedMsg = server.ReceiveMessage();
            std::string response{};
            if (receivedMsg.empty()) {
                LOG_ERROR("No message received or connection closed");
            } else {
                LOG_INFO("Received message: " + receivedMsg);
            }
            auto ret = Api::SocketMsgHandler(receivedMsg, response);
            SocketResponse resp;
            resp.retCode = ret;
            resp.retMsg = response;
            server.SendMessage(resp.ToString());
            std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_SLEEP));
        }
    } catch (const std::exception &e) {
        LOG_ERROR("Server error: " + std::string(e.what()));
        return;
    }
    LOG_INFO("Exit flag is true. Socket server exit.");
}
} // namespace vas::sched