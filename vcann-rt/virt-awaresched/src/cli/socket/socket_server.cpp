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
#include "socket_server.h"

#include <iostream>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <securec.h>

#include "logger.h"
#include "vas_security_manager.h"

namespace vas::common {
using namespace vas::security;
const fs::path SocketServer::SOCKET_DIR = "/var/run/vas";

SocketServer::SocketServer() : serverFd(0), clientSocket(0), addrLen(sizeof(serverAddr))
{
    // Initialize the buffer.
    std::fill(std::begin(buffer), std::end(buffer), 0);
}

SocketServer::~SocketServer()
{
    CloseServer();
}

bool SocketServer::RebuildRundir(const fs::path &socketDir)
{
    if (!exists(socketDir)) {
        LOG_WARN("Run dir not exist, try to rebuild.");
        if (!create_directory(socketDir)) {
            LOG_ERROR("Create run dir failed.");
            return false;
        }
        if (constexpr mode_t mode = S_IRWXU; chmod(socketDir.c_str(), mode) != 0) {
            LOG_ERROR("Chmod failed");
            return false;
        }
    }
    return true;
}

/**
 * @brief Starts the server and begins listening for incoming connections.
 * @param socketPath The socket path listen to.
 * @return true if the server is successfully started and listening.
 * @return false if socket creation, binding, or listening fails.
 */
bool SocketServer::StartServer()
{
    // Add necessary capabilities
    std::vector<__u32> caps = {
        CAP_FOWNER,
        CAP_DAC_OVERRIDE,
    };
    if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_ADD) != VAS_OK) {
        LOG_ERROR("Add capabilities failed.");
        return false;
    }
    if (!BindSocket()) {
        LOG_ERROR("Bind socket error");
        VasSecurityManager::ClearCapabilities(caps);
        return false;
    }
    // Remove capabilities
    if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_DELETE) != VAS_OK) {
        LOG_ERROR("Delete capabilities failed.");
        return false;
    }
    // Start listening for incoming connections with a backlog of 3
    if (listen(serverFd, CONNECTION_BACKLOG) < 0) {
        LOG_ERROR("Listen failed");
        CloseServer();
        return false;
    }
    LOG_INFO("Server listening. Path=" + DEFAULT_SOCKET_PATH);
    return true;
}

bool SocketServer::BindSocket()
{
    // Create a UDS socket
    serverFd = socket(AF_UNIX, SOCK_STREAM, DEFAULT_PROTOCOL);
    if (serverFd < 0) {
        LOG_ERROR("Socket creation error");
        return false;
    }

    // Configure the server address structure
    serverAddr.sun_family = AF_UNIX;
    errno_t ret = memcpy_s(serverAddr.sun_path, sizeof(serverAddr.sun_path), DEFAULT_SOCKET_PATH.c_str(),
                           DEFAULT_SOCKET_PATH.length());
    if (ret != EOK) {
        LOG_ERROR("memcpy_s failed, ret=" + std::to_string(ret));
        CloseServer();
        return false;
    }
    if (fs::exists(serverAddr.sun_path) && !fs::remove(serverAddr.sun_path)) {
        LOG_WARN("Old uds file remove failed.");
        CloseServer();
        return false;
    }

    // Rebuild run dir after reboot
    if (!RebuildRundir(SOCKET_DIR)) {
        CloseServer();
        return false;
    }

    // Bind the socket to the specified socket file
    if (bind(serverFd, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        LOG_ERROR("Bind failed");
        CloseServer();
        unlink(serverAddr.sun_path);
        return false;
    }

    // Set socket permissions
    if (constexpr mode_t mode = S_IRUSR | S_IWUSR; chmod(serverAddr.sun_path, mode) != 0) {
        LOG_ERROR("Chmod failed");
        CloseServer();
        return false;
    }

    return true;
}

/**
 * @brief Accepts a client connection and prints the client's IP address.
 *
 * This function waits for a client to connect to the server and accepts the connection.
 * If the connection is successfully accepted, it prints the client's IP address.
 * If the connection fails, an error message is printed, and the function returns false.
 *
 * @return true if the client connection is successfully accepted.
 * @return false if the connection fails.
 */
bool SocketServer::AcceptClient()
{
    // Check and close existing connections
    if (clientSocket > 0) {
        shutdown(clientSocket, SHUT_RDWR);
        close(clientSocket);
        clientSocket = -1;
    }
    // Accept a client connection
    clientSocket = accept(serverFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &addrLen);
    if (clientSocket < 0) {
        LOG_ERROR("Accept failed");
        return false;
    }

    return true;
}

/**
 * @brief Receives a message from the connected client.
 *
 * This function reads data from the client socket into a buffer and returns the received message as a string.
 * If the receive operation fails, an error message is printed, and an empty string is returned.
 *
 * @return std::string The received message from the client.
 */
std::string SocketServer::ReceiveMessage()
{
    // Clear the buffer to ensure no residual data
    std::fill(std::begin(buffer), std::end(buffer), 0);
    // Receive data from the client socket
    const ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived < 0) {
        LOG_ERROR("Receive failed");
        return "";
    }

    return std::string{buffer, static_cast<std::string::size_type>(bytesReceived)};
}

/**
 * @brief Sends a message to the connected client.
 *
 * This function sends a message to the client connected to the server using the client socket.
 * If the send operation fails, an error message is printed, and the function returns false.
 *
 * @param message The message to be sent to the client.
 * @return true if the message is successfully sent.
 * @return false if the send operation fails.
 */
bool SocketServer::SendMessage(const std::string &message)
{
    // Send the message to the client
    if (const ssize_t bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
        bytesSent != message.length()) {
        LOG_INFO("Send failed");
        return false;
    }

    return true;
}

/**
 * @brief Closes the server and all associated client connections.
 *
 * This function closes the client socket and the server socket if they are valid (greater than 0).
 * It ensures that all resources are properly released and the server is no longer listening for new connections.
 */
void SocketServer::CloseServer()
{
    if (clientSocket > 0) {
        close(clientSocket);
        clientSocket = 0;
    }

    if (serverFd > 0) {
        close(serverFd);
        serverFd = 0;
    }
}

std::string SocketResponse::ToString()
{
    std::ostringstream oss;
    oss << R"("retCode":)" << retCode << ",";
    oss << R"("retMsg":")" << retMsg << "\"";
    return oss.str();
}
} // namespace vas::common