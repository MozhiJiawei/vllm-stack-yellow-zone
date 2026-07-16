/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/command_parser.h"

// 测试用例类
class TestCommandParser : public ::testing::Test {
protected:
    void SetUp() override
    {
        parser = std::make_unique<CommandParser>("test_cmd");
    }

    std::unique_ptr<CommandParser> parser;
};

// 测试注册命令和处理逻辑
TEST_F(TestCommandParser, TestRegisterHandler)
{
    bool handler_called = false;
    // 注册一个命令和处理函数
    parser->registerHandler(
        "test",
        [&handler_called]() {
            handler_called = true;
            return true;
        },
        "Test command");

    // 调用命令解析
    int argc = 2;
    const char *argv[] = {"test_cmd", "test"};
    bool result = parser->parse(argc, const_cast<char **>(argv));

    // 验证处理函数是否被调用
    EXPECT_TRUE(handler_called);
    EXPECT_TRUE(result);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试处理已知命令
TEST_F(TestCommandParser, TestParseKnownCommand)
{
    bool handler_called = false;
    parser->registerHandler(
        "test",
        [&handler_called]() {
            handler_called = true;
            return true;
        },
        "");

    int argc = 2;
    const char *argv[] = {"test_cmd", "test"};
    bool result = parser->parse(argc, const_cast<char **>(argv));

    EXPECT_TRUE(handler_called);
    EXPECT_TRUE(result);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试处理未知命令
TEST_F(TestCommandParser, TestParseUnknownCommand)
{
    int argc = 2;
    const char *argv[] = {"test_cmd", "unknown"};
    bool result = parser->parse(argc, const_cast<char **>(argv));

    EXPECT_FALSE(result);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试无命令输入
TEST_F(TestCommandParser, TestParseNoCommand)
{
    int argc = 1;
    const char *argv[] = {"test_cmd"};
    bool result = parser->parse(argc, const_cast<char **>(argv));

    EXPECT_FALSE(result);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试帮助信息显示
TEST_F(TestCommandParser, TestPrintUsage)
{
    // 注册两个命令，一个带帮助信息，一个不带
    parser->registerHandler(
        "cmd1", []() { return true; }, "Command 1 help");
    parser->registerHandler(
        "cmd2", []() { return true; }, "");

    // 模拟printUsage的输出
    testing::internal::CaptureStdout();
    parser->printUsage();
    std::string output = testing::internal::GetCapturedStdout();

    // 验证输出是否包含正确的命令和帮助信息
    EXPECT_NE(output.find("Usage:"), std::string::npos);
    EXPECT_NE(output.find("test_cmd cmd1 : Command 1 help"), std::string::npos);
    EXPECT_NE(output.find("test_cmd cmd2"), std::string::npos);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试处理函数返回false的情况
TEST_F(TestCommandParser, TestHandlerReturnFalse)
{
    parser->registerHandler(
        "test", []() { return false; }, "");

    int argc = 2;
    const char *argv[] = {"test_cmd", "test"};
    bool result = parser->parse(argc, const_cast<char **>(argv));

    EXPECT_FALSE(result);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试帮助信息为空的情况
TEST_F(TestCommandParser, TestEmptyHelp)
{
    parser->registerHandler(
        "cmd", []() { return true; }, "");

    testing::internal::CaptureStdout();
    parser->printUsage();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("test_cmd cmd"), std::string::npos);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

// 测试帮助信息非空的情况
TEST_F(TestCommandParser, TestNonEmptyHelp)
{
    parser->registerHandler(
        "cmd", []() { return true; }, "This is help");

    testing::internal::CaptureStdout();
    parser->printUsage();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("test_cmd cmd : This is help"), std::string::npos);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}