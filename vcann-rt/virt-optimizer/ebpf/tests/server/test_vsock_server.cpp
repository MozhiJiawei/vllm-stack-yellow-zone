/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "mockcpp/Constraint.h"
#include "mockcpp/GlobalMockObject.h"
#include "mockcpp/mockcpp.hpp"

#include "server/vsock_server.h"
#include "utils.h"

namespace fs = std::filesystem;

void VsockClean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试VsockServer::init函数
TEST(VsockServerTest, TestInit)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(-1));

    int serverFd = VsockServer::init();
    EXPECT_EQ(serverFd, -1);
    VsockClean();
}

// 测试VsockServer::init函数
TEST(VsockServerTest, TestInit2)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(1));
    MOCKER(bind).stubs().with(any()).will(returnValue(-1));
    MOCKER(close).stubs().with(any()).will(returnValue(1));
    int serverFd = VsockServer::init();
    VsockClean();
}

TEST(VsockServerTest, TestInit3)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(1));
    MOCKER(bind).stubs().with(any()).will(returnValue(1));
    MOCKER(listen).stubs().with(any()).will(returnValue(-1));
    MOCKER(close).stubs().with(any()).will(returnValue(1));
    int serverFd = VsockServer::init();
    VsockClean();
}

TEST(VsockServerTest, TestInit4)
{
    MOCKER(socket).stubs().with(any()).will(returnValue(1));
    MOCKER(bind).stubs().with(any()).will(returnValue(1));
    MOCKER(listen).stubs().with(any()).will(returnValue(1));
    int serverFd = VsockServer::init();
    VsockClean();
}

TEST(VsockServerTest, Testdaemonize)
{
    MOCKER(utils::forkWithDir).stubs().with(any()).will(returnValue(1));
    VsockServer::daemonize();
    VsockClean();
}
