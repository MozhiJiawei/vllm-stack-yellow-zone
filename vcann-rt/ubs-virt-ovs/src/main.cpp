/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "config_module.h"
#include "logger.h"
#include "server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

constexpr std::chrono::milliseconds kShutdownPollInterval(200);
std::atomic<bool> g_running{true};

void SigIntHandler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_running.store(false, std::memory_order_relaxed);
    }
}

void InstallSignalHandler()
{
    struct sigaction sa {
    };
    sa.sa_handler = SigIntHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

int main()
{
    namespace config = virt::ovs::config;
    const config::ConfigCode ret = config::ConfigModule::GetInstance().Init("/etc/ubs-virt-ovs");
    if (ret != config::ConfigCode::OK) {
        LOG_ERROR << "Config module start failed, ret is :" << static_cast<uint32_t>(ret);
    }
    InstallSignalHandler();
    LOG_INFO << "Process starting";
    virt::ovs::ipc::server::Server server("/run/ubsvirt/ovs.sock");
    server.Start();

    LOG_INFO << "Server started";
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(kShutdownPollInterval);
    }
    LOG_INFO << "SIGTERM or SIGINT received, stopping service";
    server.Stop();
    LOG_INFO << "Server stopped, exiting";
    return 0;
}