/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "vsock_client.h"

#include <iostream>

#include <unistd.h>

#include <linux/vm_sockets.h>

#include "log/ebpf_logger_macros.h"
#include "utils.h"

bool sendData(const DataTable &data)
{
    int port = getPortBind();
    if (port == -1) {
        return false;
    }

    // Create and connect a vsock socket
    int sock = createAndConnectVsock(port);
    if (sock < 0) {
        return false;
    }

    // Send structure to server

    auto sentSiz = write(sock, &data, sizeof(data));

    close(sock);
    return sentSiz > 0;
}

// Create and connect a vsock socket
int createAndConnectVsock(uint32_t host_port)
{
    // Creating a vsock connection
    int sock = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (sock < 0) {
        EBPF_LOG_ERROR("Socket creation failed.");
        return -1;
    }

    struct sockaddr_vm addr = {};
    addr.svm_family = AF_VSOCK;
    addr.svm_cid = VMADDR_CID_HOST; // The host's CID is 2
    addr.svm_port = host_port;
    if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        EBPF_LOG_ERROR("Connect failed.");
        close(sock);
        return -1;
    }

    return sock;
}

int getPortBind(void)
{
    const rapidjson::Value port_value = utils::getConfigValue("bind_port");
    if (port_value.IsNull()) {
        EBPF_LOG_ERROR("Parse config failed, client can not find bind_port");
        return -1;
    };

    if (!port_value.IsInt()) {
        EBPF_LOG_ERROR("bind_port is not an integer");
        return -1;
    }

    int port = port_value.GetInt();
    if (port < PORT_LOWER_LIMIT || port > PORT_UPPER_LIMIT) {
        EBPF_LOG_ERROR("bind_port must be a positive integer between 1024 and 49151, current value: " +
                       std::to_string(port));
        return -1;
    }
    return port;
}
