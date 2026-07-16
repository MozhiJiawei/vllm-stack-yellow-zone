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

#ifndef VIRT_OVS_IPC_SERVER_SERVER_H
#define VIRT_OVS_IPC_SERVER_SERVER_H

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "auth_manager.h"
#include "common/constants.h"
#include "config_module.h"
#include "connection.h"
#include "dispatcher.h"
#include "thread_pool.h"

namespace virt::ovs::ipc::server {

using ConnPtr = std::shared_ptr<Connection>;

class Server {
public:
    explicit Server(const std::string &sockPath, size_t workers = std::thread::hardware_concurrency());
    ~Server();

    void Start();
    void Stop();

private:
    void Loop();
    bool InitListenSocket();
    bool InitEpoll();
    void HandleBusiness(const ConnPtr &conn, const std::string &req);
    void AcceptClients();
    bool HandleReadEvent(const ConnPtr &conn, int fd);
    bool HandleWriteEvent(Connection &conn, int fd) const;
    void CloseConnection(int fd);
    bool PrepareSocketDir() const;

    static std::string UidToUsername(uid_t uid);

    std::string socketPath_;
    int listenFd_{-1};
    int epollFd_{-1};

    std::atomic<bool> running_{false};
    std::thread loopThread_;

    ThreadPool pool_;
    Dispatcher dispatcher_;
    AuthManager authManager_;

    std::unordered_map<int, ConnPtr> conns_;

    int qpsLimit_{constants::DEFAULT_QPS_LIMIT};
    std::atomic<int> reqInCurrentSecond_{0};
    std::atomic<int64_t> lastSecond_{0};
};

} // namespace virt::ovs::ipc::server

#endif