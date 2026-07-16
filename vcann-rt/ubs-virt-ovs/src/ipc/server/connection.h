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

#ifndef VIRT_OVS_IPC_SERVER_CONNECTION_H
#define VIRT_OVS_IPC_SERVER_CONNECTION_H

#include <sys/types.h>
#include <cstdint>
#include <string>

namespace virt::ovs::ipc::server {

struct PeerIdentity {
    uid_t uid;
    gid_t gid;
    pid_t pid;
    std::string username;
};

class Connection {
public:
    enum class State
    {
        READ_LEN,
        READ_BODY,
        READY,
        PROCESSING,
        WRITE_RESP,
        CLOSED,
    };

    Connection(int fd, PeerIdentity identity);
    explicit Connection(int fd);

    const PeerIdentity &Identity() const
    {
        return identity_;
    }
    int Fd() const
    {
        return fd_;
    }

    bool HandleRead();
    bool HandleReadLen();
    bool HandleReadBody(bool &blocked);
    bool HandleWrite();

    bool HasRequest() const;
    std::string TakeRequest();
    void SetResponse(std::string resp, int epollFd);

    bool NeedWrite() const;

private:
    int fd_;
    PeerIdentity identity_;
    State state_{State::READ_LEN};

    uint32_t expectLen_{0};
    std::string readBuf_;
    std::string writeBuf_;
    size_t lenRead_{0};
    bool running_{false};
};

} // namespace virt::ovs::ipc::server

#endif