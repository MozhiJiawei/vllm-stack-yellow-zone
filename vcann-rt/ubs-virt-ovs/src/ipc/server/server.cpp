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

#include "server.h"
#include "logger.h"
#include "virt_ipc_code.h"

#include <fcntl.h>
#include <pwd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

using namespace virt::ovs;
using namespace virt::ovs::msg;
using namespace virt::ovs::constants;

namespace virt::ovs::ipc::server {

static void SetNonBlock(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

Server::Server(const std::string &sockPath, size_t workers) : socketPath_(sockPath), pool_(workers) {}

Server::~Server()
{
    Stop();
}

void Server::Start()
{
    LOG_INFO << "Server starting";
    running_ = true;
    pool_.Start();
    loopThread_ = std::thread(&Server::Loop, this);
}

void Server::Stop()
{
    LOG_INFO << "Server stopping";
    running_ = false;
    if (loopThread_.joinable()) {
        loopThread_.join();
    }
    pool_.Stop();
    LOG_INFO << "Server stopped";
}

bool Server::PrepareSocketDir() const
{
    namespace fs = std::filesystem;
    const fs::path socketPath(socketPath_);
    const fs::path dirPath(socketPath.parent_path());
    if (!fs::exists(dirPath)) {
        try {
            if (fs::create_directory(dirPath)) {
                LOG_INFO << "Successfully created socket directory: " << dirPath.string();
                return true;
            }
        } catch (const fs::filesystem_error &e) {
            LOG_ERROR << "Failed to create socket directory: " << e.what();
            return false;
        }
    }
    return true;
}

bool Server::InitListenSocket()
{
    listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR << "socket() failed";
        return false;
    }
    SetNonBlock(listenFd_);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath_.c_str());
    if (!PrepareSocketDir()) {
        return false;
    }
    unlink(socketPath_.c_str());

    if (bind(listenFd_, static_cast<sockaddr *>(static_cast<void *>(&addr)), sizeof(addr)) < 0 ||
        listen(listenFd_, LISTEN_BACKLOG) < 0) {
        LOG_ERROR << "bind/listen failed" << strerror(errno);
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    LOG_INFO << "listening for connections on " << socketPath_;
    return true;
}

bool Server::InitEpoll()
{
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        LOG_ERROR << "epoll_create1() failed";
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    epoll_event ev{EPOLLIN, {.fd = listenFd_}};
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) < 0) {
        LOG_ERROR << "epoll_ctl ADD listenFd failed";
        close(listenFd_);
        close(epollFd_);
        epollFd_ = -1;
        listenFd_ = -1;
        return false;
    }
    return true;
}

std::string Server::UidToUsername(uid_t uid)
{
    long bufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufSize < 0) {
        bufSize = MAX_BUFFER_SIZE;
    }
    std::vector<char> buf(bufSize);

    struct passwd pwd;
    struct passwd *result = nullptr;
    if (getpwuid_r(uid, &pwd, buf.data(), bufSize, &result) != 0 || result == nullptr) {
        return {};
    }
    return pwd.pw_name;
}

void Server::AcceptClients()
{
    bool keepReading = true;
    while (keepReading) {
        int client = accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                keepReading = false;
                continue;
            }
            LOG_WARN << "accept() failed: " << strerror(errno);
            keepReading = false;
            continue;
        }

        ucred cred{};
        socklen_t len = sizeof(cred);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
            LOG_ERROR << "getsockopt failed";
            close(client);
            continue;
        }

        PeerIdentity id{};
        id.uid = cred.uid;
        id.gid = cred.gid;
        id.pid = cred.pid;
        id.username = UidToUsername(id.uid);
        if (id.username.empty()) {
            LOG_ERROR << "Username is empty for uid " << id.uid;
            close(client);
            continue;
        }
        SetNonBlock(client);
        auto conn = std::make_shared<Connection>(client, id);
        conns_[client] = conn;

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client;
        if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, client, &ev) < 0) {
            LOG_WARN << "epoll_ctl ADD client failed: fd=" << client;
            conns_.erase(client);
            close(client);
            continue;
        }
        LOG_INFO << "accepted client, fd=" << client << " uid=" << id.uid << " user=" << id.username;
    }
}

bool Server::HandleReadEvent(const ConnPtr &conn, int fd)
{
    auto nowSec =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t lastSec = lastSecond_.load();
    if (nowSec != lastSec) {
        lastSecond_ = nowSec;
        reqInCurrentSecond_ = 0;
    }
    if (++reqInCurrentSecond_ > qpsLimit_) {
        LOG_WARN << "Rate limit exceeded, drop request, fd=" << fd;
        return true;
    }

    bool keepReading = true;
    while (keepReading) {
        if (!conn->HandleRead()) {
            return false;
        }
        if (!conn->HasRequest()) {
            keepReading = false;
            continue;
        }

        std::string req = conn->TakeRequest();
        if (!pool_.TryEnqueue([this, conn, req = std::move(req)]() mutable {
                LOG_DEBUG << "HandleBusiness scheduled fd=" << conn->Fd() << " tid=" << std::this_thread::get_id();
                this->HandleBusiness(conn, std::move(req));
            })) {
            LOG_WARN << "ThreadPool full, drop request, fd=" << fd;
            return false;
        }
    }
    return true;
}

bool Server::HandleWriteEvent(Connection &conn, int fd) const
{
    while (conn.NeedWrite()) {
        if (!conn.HandleWrite()) {
            return false;
        }
    }

    if (!conn.NeedWrite()) {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
            LOG_ERROR << "epoll_ctl MOD failed in HandleWriteEvent, fd=" << fd;
            return false;
        }
        LOG_DEBUG << "HandleWriteEvent: write done, fd=" << fd;
    }
    return true;
}

void Server::CloseConnection(int fd)
{
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    conns_.erase(fd);
    close(fd);
    LOG_INFO << "closed fd=" << fd;
}

void Server::HandleBusiness(const ConnPtr &conn, const std::string &req)
{
    LOG_INFO << "HandleBusiness begin fd=" << conn->Fd() << " tid=" << std::this_thread::get_id();
    config::ConfigModule &conf = config::ConfigModule::GetInstance();
    const auto &id = conn->Identity();
    IpcResponse resp(static_cast<uint32_t>(VirtIPCCode::OK));

    std::string authority;
    if (!AuthManager::AuthorizeUser(id.username, authority, conf)) {
        LOG_ERROR << "Permission denied: username=" << id.username;
        resp.code_ = static_cast<uint32_t>(VirtIPCCode::PERMISSION_DENIED);
    } else {
        IpcRequest ipcReq;
        {
            VirtMsgUnPacker unpacker(req);
            ipcReq.Deserialize(unpacker);
        }
        LOG_DEBUG << "IpcRequest deserialized, service=" << ipcReq.service_ << ", method=" << ipcReq.method_
                  << ", payload_size=" << ipcReq.payload_.size();

        if (!AuthManager::AuthorizeService(authority, ipcReq.service_)) {
            LOG_ERROR << "Permission denied: uid=" << id.uid << ", method=" << ipcReq.method_
                      << " service=" << ipcReq.service_;
            resp.code_ = static_cast<uint32_t>(VirtIPCCode::PERMISSION_DENIED);
        } else {
            try {
                resp = dispatcher_.Dispatch(ipcReq);
            } catch (const std::exception &e) {
                LOG_ERROR << "Dispatch request failed: " << e.what();
                resp.code_ = static_cast<uint32_t>(VirtIPCCode::INTERNAL_ERROR);
            }
        }
    }

    VirtMsgPacker packer;
    resp.Serialize(packer);
    conn->SetResponse(packer.String(), epollFd_);

    LOG_DEBUG << "IpcResponse serialized, fd=" << conn->Fd() << ", code=" << resp.code_
              << ", payload_size=" << resp.payload_.size();
}

void Server::Loop()
{
    if (!InitListenSocket() || !InitEpoll()) {
        return;
    }

    epoll_event events[MAX_EPOLL_EVENTS];
    while (running_) {
        int n = epoll_wait(epollFd_, events, MAX_EPOLL_EVENTS, EPOLL_WAIT_TIMEOUT_MS);
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t evt = events[i].events;

            if (fd == listenFd_) {
                AcceptClients();
                continue;
            }

            auto it = conns_.find(fd);
            if (it == conns_.end()) {
                continue;
            }
            auto &conn = it->second;

            if ((evt & EPOLLIN) && !HandleReadEvent(conn, fd)) {
                CloseConnection(fd);
                continue;
            }
            if ((evt & EPOLLOUT) && !HandleWriteEvent(*conn, fd)) {
                CloseConnection(fd);
            }
        }
    }

    close(listenFd_);
    listenFd_ = -1;
    close(epollFd_);
    epollFd_ = -1;

    LOG_INFO << "Event loop exited";
}

} // namespace virt::ovs::ipc::server