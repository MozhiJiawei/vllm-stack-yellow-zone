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

#include "test_socket_client.h"

#include <securec.h>

#include <mockcpp/mockcpp.hpp>

#include "socket_client.h"

namespace vas::ut::common {
using namespace vas::common;

void TestSocketClient::SetUp()
{
    Test::SetUp();
}

void TestSocketClient::TearDown()
{
    GlobalMockObject::verify();
    Test::TearDown();
}

TEST_F(TestSocketClient, testConnectToServer)
{
    MOCKER_CPP(&SocketClient::CloseConnection, void (SocketClient::*)()).stubs().will(ignoreReturnValue());
    MOCKER(close).stubs().will(returnValue(1));
    SocketClient socketClient;
    MOCKER(socket).stubs().will(returnValue(-1)).then(returnValue(1));
    auto ret = socketClient.ConnectToServer();
    EXPECT_FALSE(ret);
    MOCKER(connect).stubs().will(returnValue(-1)).then(returnValue(1));
    MOCKER(close).stubs().will(returnValue(1));
    ret = socketClient.ConnectToServer();
    EXPECT_FALSE(ret);
    ret = socketClient.ConnectToServer();
    EXPECT_TRUE(ret);
}

TEST_F(TestSocketClient, testConnectToServerException)
{
    MOCKER_CPP(&SocketClient::CloseConnection, void (SocketClient::*)()).stubs().will(ignoreReturnValue());
    MOCKER(close).stubs().will(returnValue(1));
    SocketClient socketClient;
    MOCKER(socket).stubs().will(returnValue(1));
    MOCKER(memcpy_s).stubs().will(returnValue(-1));
    auto ret = socketClient.ConnectToServer();
    EXPECT_FALSE(ret);
}

ssize_t ClientSendMock(int fd, const void *buf, size_t n, int flags)
{
    auto *buff = static_cast<const char *>(buf);
    if (*buff == 'a') {
        return 1;
    }
    return -1;
};

TEST_F(TestSocketClient, testSendMessage)
{
    MOCKER_CPP(&SocketClient::CloseConnection, void (SocketClient::*)()).stubs().will(ignoreReturnValue());
    MOCKER(close).stubs().will(returnValue(1));
    SocketClient socketClient;
    socketClient.isConnected = false;
    auto ret = socketClient.SendMessage("a");
    EXPECT_FALSE(ret);
    socketClient.isConnected = true;
    MOCKER(send).stubs().will(invoke(ClientSendMock));
    ret = socketClient.SendMessage("b");
    EXPECT_FALSE(ret);
    ret = socketClient.SendMessage("a");
    EXPECT_TRUE(ret);
}

ssize_t ClientRecvMockErr(int fd, void *buf, size_t n, int flags)
{
    auto *buff = static_cast<char *>(buf);
    *buff = 'a';
    return -1;
}

ssize_t ClientRecvMockZero(int fd, void *buf, size_t n, int flags)
{
    auto *buff = static_cast<char *>(buf);
    *buff = 'a';
    return 0;
}

ssize_t ClientRecvMock(int fd, void *buf, size_t n, int flags)
{
    auto *buff = static_cast<char *>(buf);
    *buff = 'a';
    return 1;
}

TEST_F(TestSocketClient, testReceiveMessage)
{
    MOCKER_CPP(&SocketClient::CloseConnection, void (SocketClient::*)()).stubs().will(ignoreReturnValue());
    MOCKER(close).stubs().will(returnValue(1));
    MOCKER(recv)
        .stubs()
        .will(returnValue(static_cast<ssize_t>(-1)))
        .then(returnValue(static_cast<ssize_t>(0)))
        .then(invoke(ClientRecvMock));
    SocketClient socketClient;
    socketClient.isConnected = false;
    auto ret = socketClient.ReceiveMessage();
    EXPECT_EQ(ret, "");
    socketClient.isConnected = true;
    ret = socketClient.ReceiveMessage();
    EXPECT_EQ(ret, "");
    ret = socketClient.ReceiveMessage();
    EXPECT_EQ(ret, "");
    socketClient.isConnected = true;
    ret = socketClient.ReceiveMessage();
    EXPECT_EQ(ret, "a");
}

TEST_F(TestSocketClient, testCloseConnection)
{
    SocketClient socketClient;
    socketClient.clientSocket = 1;
    MOCKER(close).stubs().will(returnValue(1));
    socketClient.CloseConnection();
    socketClient.clientSocket = 0;
    socketClient.CloseConnection();
}

TEST_F(TestSocketClient, testSendSingleMessage)
{
    MOCKER_CPP(&SocketClient::CloseConnection, void (SocketClient::*)()).stubs().will(ignoreReturnValue());
    MOCKER(close).stubs().will(returnValue(1));
    MOCKER_CPP(&SocketClient::ConnectToServer, bool (SocketClient::*)())
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    auto ret = SendSingleMessage("a");
    EXPECT_FALSE(ret);

    MOCKER_CPP(&SocketClient::SendMessage, bool (SocketClient::*)(const std::string &))
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    ret = SendSingleMessage("a");
    EXPECT_FALSE(ret);

    MOCKER_CPP(&SocketClient::ReceiveMessage, std::string(SocketClient::*)())
        .stubs()
        .will(returnValue(std::string("a")));
    ret = SendSingleMessage("a");
    EXPECT_TRUE(ret);
}
} // namespace vas::ut::common