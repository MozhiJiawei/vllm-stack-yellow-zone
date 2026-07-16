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
#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include <filesystem>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "def.h"

namespace vas::common {
namespace fs = std::filesystem;

struct SocketResponse {
    int retCode;
    std::string retMsg;

    std::string ToString();
};

class SocketServer {
public:
    /**
     * @brief Constructor for SocketServer
     *
     * Initializes the server socket and prepares for connection
     */
    SocketServer();

    /**
     * @brief Constructor for SocketServer
     *
     * Initializes the server socket and prepares for connection
     */
    ~SocketServer();

    /**
     * @brief Start the server on a specified port
     *
     * @param socketPath Port number to bind and listen on
     * @return bool True if server starts successfully, false otherwise
     */
    bool StartServer();

    /**
     * @brief Accept an incoming client connection
     *
     * @return bool True if a client is successfully connected, false otherwise
     * @note Blocks until a client connects
     */
    bool AcceptClient();

    /**
     * @brief Receive a message from the connected client
     *
     * @return std::string Received message
     * @note Blocks until a message is received
     */
    std::string ReceiveMessage();

    /**
     * @brief Send a message to the connected client
     *
     * @param message Message string to be sent
     * @return bool True if message sent successfully, false otherwise
     */
    bool SendMessage(const std::string &message);

    /**
     * @brief Close the server and all active connections
     *
     * Releases socket resources and stops the server
     */
    void CloseServer();

private:
    /** @brief Default protocol value for socket creation */
    static constexpr int DEFAULT_PROTOCOL = 0;
    /** @brief Maximum number of pending connections in the listen queue */
    static constexpr int CONNECTION_BACKLOG = 3;
    static const fs::path SOCKET_DIR;

    /** @brief Server socket file descriptor */
    int serverFd;

    /** @brief Connected client socket file descriptor */
    int clientSocket;

    /** @brief Server address structure for binding */
    sockaddr_un serverAddr{};

    /** @brief Client address structure for incoming connections */
    sockaddr_in clientAddr{};

    /** @brief Length of the client address structure */
    socklen_t addrLen;

    /**
     * @brief Receive buffer for incoming network data
     * @details Fixed size buffer of 1024 bytes
     */
    char buffer[1024]{};

    /**
     * @brief bind socket
     *
     * @return True if binding successfully, false otherwise
     */
    bool BindSocket();

    bool RebuildRundir(const fs::path &socketDir);
};

} // namespace vas::common

#endif // SOCKET_SERVER_H
