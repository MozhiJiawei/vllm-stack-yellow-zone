/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <iostream>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"

#include "common/utils.h"

void Clean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(ForkWithDirTest, ForkFailure)
{
    // mockcpp mocking 全局 fork 函数
    MOCKER(fork).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ForkSuccess)
{
    MOCKER(fork).expects(once()).will(returnValue(1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, 1);
    Clean();
}

TEST(ForkWithDirTest, SetsidFailure)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ChdirFailure)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(1));
    MOCKER(umask).expects(once()).will(returnValue((unsigned int)0));
    MOCKER(chdir).expects(once()).will(returnValue(-1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, -1);
    Clean();
}

TEST(ForkWithDirTest, ForkWithDirSuccess)
{
    MOCKER(fork).expects(once()).will(returnValue(0));
    MOCKER(setsid).expects(once()).will(returnValue(1));
    MOCKER(umask).expects(once()).will(returnValue(0));
    MOCKER(chdir).expects(once()).will(returnValue(1));
    MOCKER(close).stubs().will(returnValue(1));

    pid_t result = utils::forkWithDir("/xxx");
    EXPECT_EQ(result, 0);

    Clean();
}

TEST(GetDaemonPIDTest, FileDoesNotExist)
{
    const char *nonExistentFile = "nonexist.pid";
    pid_t pid = utils::getDaemonPID(nonExistentFile);
    EXPECT_EQ(pid, 0);
}

TEST(CheckRunningTest, CheckRunningSuccess)
{
    MOCKER(std::filesystem::exists).stubs().will(returnValue(0));

    bool result = utils::checkRunning("/xxx", "name");
    EXPECT_EQ(result, false);
    Clean();
}

TEST(UtilsTest, getConfigValueCase1)
{
    const std::string str = "xxx";
    rapidjson::Value result = utils::getConfigValue(str);

    EXPECT_EQ(result, rapidjson::Value());
    Clean();
}

TEST(UtilsTest, getConfigValueCase2)
{
    const std::string str = ".xxx";
    rapidjson::Value result = utils::getConfigValue(str);
    EXPECT_EQ(result, rapidjson::Value());
    Clean();
}

TEST(UtilsTest, getConfigValueCase3)
{
    const std::string str = "sampling_interval.";
    rapidjson::Value result = utils::getConfigValue(str);

    EXPECT_EQ(result, rapidjson::Value());
    Clean();
}