/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <gtest/gtest.h>
#include "common/log/ebpf_logger.h"

class EbpfLoggerTest : public ::testing::Test {
protected:
    EbpfLogger &logger = EbpfLogger::getInstance();

    void SetUp() override
    {
        logger.cleanup();
    }

    void TearDown() override
    {
        logger.cleanup();
    }
};

TEST_F(EbpfLoggerTest, InitReturnsTrueForValidPath)
{
    bool ret = logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::INFO, false, false, 10 * 1024 * 1024);
    EXPECT_TRUE(ret);
    EXPECT_TRUE(logger.isRunningForTest());
}

TEST_F(EbpfLoggerTest, CleanupWithoutInitIsSafe)
{
    logger.cleanup();
    SUCCEED();
}

TEST_F(EbpfLoggerTest, InitAndCleanup)
{
    bool ret = logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::INFO, false, false, 10 * 1024 * 1024);
    EXPECT_TRUE(ret);
    logger.cleanup();
    EXPECT_FALSE(logger.isRunningForTest());
}

TEST_F(EbpfLoggerTest, LogMessageFilteredOut)
{
    logger.init("/tmp/test_ebpf.log", EbpfLogger::LogLevel::WARNING, false, false, 10 * 1024 * 1024);

    size_t initialSize = logger.getLogQueueSizeForTest();

    // DEBUG < WARNING，不入队
    logger.logMessage(EbpfLogger::LogLevel::DEBUG, "debug message", __FILE__, __FUNCTION__, __LINE__);

    size_t afterSize = logger.getLogQueueSizeForTest();

    EXPECT_EQ(initialSize, afterSize);

    logger.cleanup();
}

TEST_F(EbpfLoggerTest, LevelToStringCase1)
{
    logger.logMessage(EbpfLogger::LogLevel::FATAL, "debug message", __FILE__, __FUNCTION__, __LINE__);
    SUCCEED();
}