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
#ifndef SOCKET_CLIENT_H
#define SOCKET_CLIENT_H

#include <string>

#include <sys/socket.h>
#include <sys/un.h>

#include "def.h"

namespace vas::common {

const size_t MAX_RECEIVE_SIZE = 2 * 1024 * 1024;
class SocketClient {
public:
    /**
     * @brief Constructor for SocketClient
     *
     * Initializes the socket client and prepares it for connection
     */
    SocketClient();

    /**
     * @brief Destructor for SocketClient
     *
     * Closes any open connections and releases resources
     */
    ~SocketClient();

    /**
     * @brief Connect to a server
     *
     * @param socketPath
     * @return bool True if connection is successful, false otherwise
     */
    bool ConnectToServer();

    /**
     * @brief Send a message to the connected server
     *
     * @param message Message string to be sent
     * @return bool True if message sent successfully, false otherwise
     */
    bool SendMessage(const std::string &message);

    /**
     * @brief Receive a message from the server
     *
     * @return std::string Received message
     * @note Blocks until a message is received
     */
    std::string ReceiveMessage();

    /**
     * @brief Close the current network connection
     *
     * Releases socket resources and resets connection state
     */
    void CloseConnection();

private:
    /** @brief Socket file descriptor for network communication */
    int clientSocket;

    /** @brief Server address structure for network connection */
    sockaddr_un serverAddr{};

    /**
     * @brief Receive buffer for incoming network data
     * @details Fixed size buffer of 1024 bytes
     */
    char buffer[1024];

    /** @brief Connection status flag */
    bool isConnected;
};

bool SendSingleMessage(const std::string &message);
} // namespace vas::common

#endif // SOCKET_CLIENT_H
