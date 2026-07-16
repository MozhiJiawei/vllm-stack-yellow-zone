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
#include "test_logger.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <mockcpp/mockcpp.hpp>

#include "logger.h"

using namespace vas::common;

namespace vas::ut::logger {

TEST_F(TestLogger, testRotateCheckWithOutFile)
{
    Logger logger;
    auto ret = logger.RotateCheck();
    EXPECT_EQ(ret, VAS_ERROR);
}

TEST_F(TestLogger, testRotateCheckHasFile)
{
    Logger logger;
    auto testPath = std::filesystem::current_path() / "test_log_file";
    logger.logFilePath_ = testPath;
    auto ret = logger.RotateCheck();
    EXPECT_EQ(ret, VAS_OK);
    MOCKER(chmod).stubs().will(returnValue(-1)).then(returnValue(0));
    ret = logger.RotateCheck(true);
    EXPECT_EQ(ret, VAS_ERROR);
    ret = logger.RotateCheck(true);
    EXPECT_EQ(ret, VAS_OK);
    std::filesystem::remove_all(testPath);
}

TEST_F(TestLogger, testRotateCheckHasFile2)
{
    Logger logger;
    logger.maxSize_ = 0;
    auto ret = logger.RotateCheck(false);
    EXPECT_EQ(ret, VAS_ERROR);
}

TEST_F(TestLogger, testRotateCheckHasFile3)
{
    Logger logger;
    logger.maxSize_ = 0;
    auto testPath = std::filesystem::current_path() / "test_log_file3";
    MOCKER(chmod).stubs().will(returnValue(-1));
    auto ret = logger.RotateCheck(false);
    EXPECT_EQ(ret, VAS_ERROR);
    std::filesystem::remove_all(testPath);
}

TEST_F(TestLogger, testLoggerInitialization)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 200 * 1024 * 1024, 5, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
    std::string logFilePath = logPath_ + "/" + logFile_;
    EXPECT_TRUE(std::filesystem::exists(logPath_));
    EXPECT_TRUE(std::filesystem::exists(logFilePath));
}

TEST_F(TestLogger, testLoggerRotation)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 100, 5, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
    for (int i = 0; i < 1000; ++i) {
        Logger::Instance().Log(Level::ERROR, "Test message", __FILE__, __LINE__, __func__);
    }
    std::string baseLogPath = logPath_ + "/" + logFile_;
    int fileCount = 0;
    for (int i = 1; i <= 5; ++i) {
        std::string filePath = baseLogPath + "." + std::to_string(i);
        if (std::filesystem::exists(filePath)) {
            ++fileCount;
        }
    }
    EXPECT_LE(fileCount, 5);
}

TEST_F(TestLogger, testLoggerOutputToStdout)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 200 * 1024 * 1024, 5, OutputType::STDOUT);
    EXPECT_EQ(ret, VAS_OK);
}

TEST_F(TestLogger, testLoggerRotateFiles)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 100, 5, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
    std::string baseLogPath = logPath_ + "/" + logFile_;
    for (int i = 1; i <= 5; ++i) {
        std::string filePath = baseLogPath + "." + std::to_string(i);
        std::ofstream file(filePath);
        file << "Test content";
        file.close();
    }
    Logger::Instance().RotateFiles();
    EXPECT_TRUE(std::filesystem::exists(baseLogPath + ".5"));
    for (int i = 2; i <= 5; ++i) {
        std::string filePath = baseLogPath + "." + std::to_string(i);
        EXPECT_TRUE(std::filesystem::exists(filePath));
    }
}

TEST_F(TestLogger, testLoggerInitException)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 1, 1, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
    MOCKER(fs::exists).stubs().will(returnValue(false));
    ret = Logger::Instance().Init("logPath_", logFile_, MAX_LOGFILESIZE, MAX_LOGFILE, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
}

TEST_F(TestLogger, testLoggerRotateCheck)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 100, 5, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);

    std::ofstream file(logPath_ + "/" + logFile_);
    for (int i = 0; i < 300; ++i) {
        Logger::Instance().Log(Level::ERROR, "Test message", __FILE__, __LINE__, __func__);
    }
    file.close();
    Logger::Instance().RotateCheck();
    EXPECT_TRUE(std::filesystem::exists(logPath_ + "/" + logFile_));
    EXPECT_TRUE(std::filesystem::exists(logPath_ + "/" + logFile_ + ".1"));
}

TEST_F(TestLogger, testLoggerLogLevelFilter)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, 200 * 1024 * 1024, 5, OutputType::FILE);
    EXPECT_EQ(ret, VAS_OK);
    Logger::Instance().logLevel_ = Level::INFO;
    std::string logFilePath = logPath_ + "/" + logFile_;
    std::ifstream file(logFilePath);
    std::string line;
    bool debugFound = false;
    bool infoFound = false;
    bool warnFound = false;
    bool errorFound = false;
    Logger::Instance().Log(Level::DEBUG, "Test message", __FILE__, __LINE__, __func__);
    Logger::Instance().Log(Level::INFO, "Test message", __FILE__, __LINE__, __func__);
    Logger::Instance().Log(Level::WARNING, "Test message", __FILE__, __LINE__, __func__);
    Logger::Instance().Log(Level::ERROR, "Test message", __FILE__, __LINE__, __func__);
    while (std::getline(file, line)) {
        if (line.find("DEBUG") != std::string::npos) {
            debugFound = true;
        }
        if (line.find("INFO") != std::string::npos) {
            infoFound = true;
        }
        if (line.find("WARN") != std::string::npos) {
            warnFound = true;
        }
        if (line.find("ERROR") != std::string::npos) {
            errorFound = true;
        }
    }
    EXPECT_FALSE(debugFound);
    EXPECT_TRUE(infoFound);
    EXPECT_TRUE(warnFound);
    EXPECT_TRUE(errorFound);
}

TEST_F(TestLogger, testLoggerLogLevelFilter2)
{
    VasRet ret = Logger::Instance().Init(logPath_, logFile_, MAX_LOGFILESIZE, MAX_LOGFILE, OutputType::FILE);

    Logger::Instance().logLevel_ = Level::INFO;
    Logger::Instance().outputType_ = OutputType::STDOUT;
    std::string logFilePath = logPath_ + "/" + logFile_;
    std::ifstream file(logFilePath);
    std::string line;
    Logger::Instance().Log(Level::INFO, "Test message", __FILE__, __LINE__, __func__);
    EXPECT_EQ(ret, VAS_OK);
    Logger::Instance().outputType_ = OutputType::NONE;
}
} // namespace vas::ut::logger