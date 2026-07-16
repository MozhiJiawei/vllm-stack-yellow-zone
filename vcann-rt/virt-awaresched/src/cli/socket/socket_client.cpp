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
#include "socket_client.h"

#include <unistd.h>
#include <iostream>

#include <securec.h>

namespace vas::common {

SocketClient::SocketClient() : clientSocket(0), isConnected(false), buffer()
{
    // Initialize the buffer.
    std::fill(std::begin(buffer), std::end(buffer), 0);
}

SocketClient::~SocketClient()
{
    CloseConnection();
}

/**
 * @brief Attempts to establish a TCP connection to the specified server.
 *
 * This function creates a TCP socket, configures the server address structure,
 * converts the IP address string to binary form, and attempts to connect to the server.
 *
 * @param socketPath
 * @return true if the connection is successfully established.
 * @return false if socket creation, address conversion, or connection fails.
 *
 * @note On failure:
 *       - Prints error messages to std::cerr
 *       - Automatically cleans up resources (closes socket)
 *       - Resets internal connection state
 */
bool SocketClient::ConnectToServer()
{
    // Create TCP socket (IPv4, stream-oriented, default protocol)
    clientSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return false;
    }

    // Configure server address structure
    serverAddr.sun_family = AF_UNIX;
    errno_t ret = memcpy_s(serverAddr.sun_path, sizeof(serverAddr.sun_path), DEFAULT_SOCKET_PATH.c_str(),
                           DEFAULT_SOCKET_PATH.length());
    if (ret != EOK) {
        std::cerr << "memcpy_s failed, ret=" << ret << std::endl;
        close(clientSocket);
        clientSocket = -1;
        return false;
    }

    // Initiate connection to remote server
    if (connect(clientSocket, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        CloseConnection();
        return false;
    }

    isConnected = true;
    return true;
}

/**
 * @brief Sends a message to the connected server.
 *
 * This function checks if the client is currently connected to a server. If connected,
 * it sends the provided message through the established socket. If the send operation
 * fails, an error message is printed, and the function returns false.
 *
 * @param message The message to be sent to the server.
 * @return true if the message is successfully sent.
 * @return false if the client is not connected or if the send operation fails.
 *
 * @note On failure:
 *       - Prints error messages to std::cerr
 *       - Does not close the socket or reset the connection state
 *
 * @note On success:
 *       - Prints the sent message to std::cout
 */
bool SocketClient::SendMessage(const std::string &message)
{
    if (!isConnected) {
        std::cerr << "Not connected to server" << std::endl;
        return false;
    }

    ssize_t bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
    if (bytesSent < 0) {
        std::cerr << "Send failed" << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Receives a message from the connected server.
 *
 * This function checks if the client is currently connected to a server. If connected,
 * it clears the buffer, receives a message from the server, and returns the received message.
 * If the receive operation fails or the server disconnects, appropriate messages are printed,
 * and the function returns an empty string.
 *
 * @return std::string The received message from the server.
 *
 * @note On failure:
 *       - Prints error messages to std::cerr
 *       - Closes the connection and resets the internal state if the server disconnects
 *
 * @note On success:
 *       - Prints the received message to std::cout
 */
std::string SocketClient::ReceiveMessage()
{
    if (!isConnected) {
        std::cerr << "Not connected to server" << std::endl;
        return "";
    }

    // Clear the buffer to ensure no residual data
    std::fill(std::begin(buffer), std::end(buffer), 0);
    std::string receiveStr;
    ssize_t bytesReceived;

    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived == 0) {
        std::cout << "Server disconnected" << std::endl;
    }

    while (bytesReceived > 0) {
        // Ensure that the end of the buffer is the string terminator.
        buffer[bytesReceived] = '\0';

        // Append received data
        receiveStr.append(buffer, bytesReceived);

        // Check if the received data exceeds the maximum size
        if (receiveStr.size() > MAX_RECEIVE_SIZE) {
            std::cerr << "Received data exceeds 2MB" << std::endl;
            CloseConnection();
            return "";
        }

        // If the number of received bytes is less than the buffer size, it indicates that this is the last data block.
        if (bytesReceived < sizeof(buffer) - 1) {
            break;
        }

        // Continue receiving the next block of data
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    }

    // Handling Error Situations
    if (bytesReceived < 0) {
        std::cerr << "Receive failed" << std::endl;
        CloseConnection();
        return "";
    }

    CloseConnection();
    return receiveStr;
}

/**
 * @brief Closes the connection to the server and cleans up resources.
 *
 * This function checks if the client socket is valid (greater than 0). If valid,
 * it closes the socket and resets the socket descriptor to 0. It also sets the
 * internal connection state to false to indicate that the client is no longer connected.
 *
 * @note This function is idempotent; calling it multiple times will not cause issues.
 *
 * @note On success:
 *       - Closes the socket and resets the socket descriptor.
 *       - Sets the internal connection state to false.
 *
 * @note On failure:
 *       - If the socket is already closed or invalid, the function does nothing.
 */
void SocketClient::CloseConnection()
{
    if (clientSocket > 0) {
        close(clientSocket);
        clientSocket = 0;
    }
    isConnected = false;
}

/**
 * @brief Sends a single message to a server and receives a response.
 *
 * This function attempts to connect to a server, send a message, and receive a response.
 * It uses RAII (Resource Acquisition Is Initialization) to ensure that the socket client
 * is properly initialized and cleaned up. If any step fails, appropriate error messages
 * are logged, and the function returns false.
 *
 * @param message The message to be sent to the server.
 * @return true if the message is successfully sent and a non-empty response is received.
 * @return false if any step fails or if an empty response is received.
 */
bool SendSingleMessage(const std::string &message)
{
    // Use RAII to ensure resources are properly released
    SocketClient client;
    try {
        // Connect to the server
        if (!client.ConnectToServer()) {
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }

        // Send the message
        if (!client.SendMessage(message)) {
            std::cerr << "Failed to send message to server" << std::endl;
            return false;
        }

        // Receive the server response
        if (const std::string response = client.ReceiveMessage(); !response.empty()) {
            std::cout << response << std::endl;
        }
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Unexpected error in SendSingleMessage: " << e.what() << std::endl;
        client.CloseConnection();
        return false;
    }
}

} // namespace vas::common