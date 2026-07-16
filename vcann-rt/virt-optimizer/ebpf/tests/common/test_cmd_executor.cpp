/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <cstdio>
#include <filesystem>
#include <iostream>

#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"

void Clean2()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(CmdExecutorTest, RunCommandFail)
{
    CmdExecutor exc("123");
    FILE *mockPipe = nullptr;
    MOCKER(popen).stubs().with(any()).will(returnValue(mockPipe));
    MOCKER(pclose).stubs().with(any()).will(returnValue(0));
    std::pair<bool, std::string> res = exc.runCommand("che");
    EXPECT_EQ(res.first, false);
    Clean2();
}

TEST(CmdExecutorTest, NoHostnameFail)
{
    CmdExecutor exc("");
    FILE *mockPipe = nullptr;
    MOCKER(popen).expects(once()).will(returnValue(mockPipe));
    std::pair<bool, std::string> res = exc.runCommand("che");
    EXPECT_EQ(res.first, false);
    Clean2();
}

TEST(CmdExecutorTest, RunCommandExited)
{
    CmdExecutor exc("123");
    int tmp = 1;
    FILE *mockPipe = popen("./", "r");
    MOCKER(popen).expects(once()).with(any()).will(returnValue(mockPipe));
    MOCKER(pclose).expects(once()).with(any()).will(returnValue(0));
    std::pair<bool, std::string> res = exc.runCommand("che");
    EXPECT_EQ(res.first, true);
    Clean2();
}

TEST(CmdExecutorTest, FgetsSuccess)
{
    CmdExecutor exc("123");
    int tmp = 1;
    FILE *mockPipe = popen("./", "r");
    char mock_buffer[256] = "This is mocked input from fgets\n";
    MOCKER(popen).expects(once()).with(any()).will(returnValue(mockPipe));
    MOCKER(pclose).expects(once()).with(any()).will(returnValue(1));
    // 模拟两次成功读取 + 一次EOF
    MOCKER(fgets)
        .expects(exactly(2)) // 总期望调用3次（调整为你预期的循环次数）
        .with(any())         // 如果需要匹配参数：.with(eq(buf), eq(sizeof(buf)), eq(pipe))
        .will(returnObjectList(static_cast<char *>(mock_buffer), static_cast<char *>(nullptr)));
    std::pair<bool, std::string> res = exc.runCommand("che");
    EXPECT_EQ(res.first, false);
    Clean2();
}

TEST(CmdExecutorTest, RunCommandSuccess)
{
    CmdExecutor exc("123");
    int tmp = 1;
    FILE *mockPipe = popen("./", "r");
    MOCKER(popen).expects(once()).with(any()).will(returnValue(mockPipe));
    MOCKER(pclose).expects(once()).with(any()).will(returnValue(1));
    std::pair<bool, std::string> res = exc.runCommand("che");
    EXPECT_EQ(res.first, false);
    Clean2();
}
