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

#include "test_socket_server.h"

#include <securec.h>
#include <mockcpp/mockcpp.hpp>

#include "socket_server.h"
#include "vas_security_manager.h"

namespace vas::ut::common {
using namespace vas::common;
inline constexpr int NUMBER_NEGATIVE_ONE = -1;
inline constexpr int NUMBER_ONE = 1;
inline constexpr int NUMBER_TEN = 10;
void TestSocketServer::SetUp()
{
    Test::SetUp();
}

void TestSocketServer::TearDown()
{
    GlobalMockObject::verify();
    Test::TearDown();
}

void SocketServerCloseServer(SocketServer *_this) {}

TEST_F(TestSocketServer, testStartServerException)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(1));
    MOCKER(socket).stubs().will(returnValue(-1)).then(returnValue(1));
    MOCKER(vas::security::VasSecurityManager::ModifyEffectiveCapabilities).stubs().will(returnValue(-1));
    SocketServer socketServer;
    auto ret = socketServer.StartServer();
    EXPECT_FALSE(ret);
}

TEST_F(TestSocketServer, testStartServerException2)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(1));
    MOCKER(socket).stubs().will(returnValue(-1)).then(returnValue(1));
    MOCKER(vas::security::VasSecurityManager::ModifyEffectiveCapabilities)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));
    MOCKER(bind).stubs().will(returnValue(0));
    SocketServer socketServer;
    auto ret = socketServer.StartServer();
    EXPECT_FALSE(ret);
}

TEST_F(TestSocketServer, testRebuildRundir)
{
    SocketServer socketServer{};
    auto testPath = std::filesystem::current_path() / "test_socket_dir";
    auto ret = socketServer.RebuildRundir(testPath);
    EXPECT_TRUE(ret);
    ret = socketServer.RebuildRundir(testPath);
    EXPECT_TRUE(ret);
    std::filesystem::remove_all(testPath);
}

TEST_F(TestSocketServer, testRebuildRundirException)
{
    SocketServer socketServer{};
    auto testPath = std::filesystem::current_path() / "test_socket_dir";
    MOCKER(std::filesystem::create_directory).stubs().will(returnValue(false));
    auto ret = socketServer.RebuildRundir(testPath);
    EXPECT_FALSE(ret);
    MOCKER(chmod).stubs().will(returnValue(1));
    ret = socketServer.RebuildRundir(testPath);
    EXPECT_FALSE(ret);
    std::filesystem::remove_all(testPath);
}

TEST_F(TestSocketServer, testBindSocket)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(1));
    SocketServer socketServer{};
    MOCKER(memcpy_s).stubs().will(returnValue(-1));
    auto ret = socketServer.BindSocket();
    EXPECT_FALSE(ret);
}

TEST_F(TestSocketServer, testAcceptClient)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(NUMBER_ONE));
    SocketServer socketServer{};
    MOCKER(accept).stubs().will(returnValue(NUMBER_NEGATIVE_ONE)).then(returnValue(NUMBER_ONE));
    auto ret = socketServer.AcceptClient();
    EXPECT_FALSE(ret);
    ret = socketServer.AcceptClient();
    EXPECT_TRUE(ret);
}

TEST_F(TestSocketServer, testAcceptClientException)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(NUMBER_ONE));
    SocketServer socketServer{};
    socketServer.clientSocket = 1;
    MOCKER(accept).stubs().will(returnValue(0));
    auto ret = socketServer.AcceptClient();
    EXPECT_TRUE(ret);
}

ssize_t ServerRecvMock(int fd, void *buf, size_t n, int flags)
{
    auto *buff = static_cast<char *>(buf);
    *buff = 'a';
    return NUMBER_ONE;
}

TEST_F(TestSocketServer, testReceiveMessage)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(NUMBER_ONE));
    SocketServer socketServer{};
    MOCKER(recv).stubs().will(returnValue(static_cast<ssize_t>(NUMBER_NEGATIVE_ONE))).then(invoke(ServerRecvMock));
    auto ret = socketServer.ReceiveMessage();
    EXPECT_EQ(ret, "");
    ret = socketServer.ReceiveMessage();
    EXPECT_EQ(ret, "a");
}

ssize_t ServerSendMock(int fd, const void *buf, size_t n, int flags)
{
    auto *buff = static_cast<const char *>(buf);
    if (*buff == 'a') {
        return NUMBER_ONE;
    }
    return NUMBER_TEN;
};

TEST_F(TestSocketServer, testSendMessage)
{
    MOCKER_CPP(&SocketServer::CloseServer, void (SocketServer::*)()).stubs().will(invoke(SocketServerCloseServer));
    MOCKER(close).stubs().will(returnValue(NUMBER_ONE));
    MOCKER(send).stubs().will(invoke(ServerSendMock));
    SocketServer socketServer;
    auto ret = socketServer.SendMessage("b");
    EXPECT_FALSE(ret);
    ret = socketServer.SendMessage("a");
    EXPECT_TRUE(ret);
}

TEST_F(TestSocketServer, testCloseServer)
{
    MOCKER(close).stubs().will(returnValue(NUMBER_ONE));
    SocketServer socketServer;
    socketServer.clientSocket = NUMBER_ONE;
    socketServer.serverFd = NUMBER_ONE;
    socketServer.CloseServer();
}
} // namespace vas::ut::common