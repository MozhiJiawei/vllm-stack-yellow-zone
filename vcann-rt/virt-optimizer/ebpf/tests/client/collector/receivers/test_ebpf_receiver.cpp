/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <atomic>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "client/collector/receivers/ebpf_receiver.h"
#include "client/collector/receivers/vsock_client.h"

TEST(EBPFReceiverTest, PerformanceTest)
{
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程运行 EBPFReceiver
        auto &receiver = EBPFReceiver::getInstance();
        receiver.launch();
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    } else {
        // 父进程控制子进程
        std::this_thread::sleep_for(std::chrono::seconds(1));
        kill(pid, SIGTERM);       // 终止子进程
        waitpid(pid, nullptr, 0); // 等待回收
    }
}

TEST(EBPFReceiverTest, SetSamplingIntervalTest)
{
    // 获取实例
    EBPFReceiver &instance1 = EBPFReceiver::getInstance();
    instance1.setSamplingInterval(10);
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(EBPFReceiverTest, GetInstanceTest)
{
    // 获取实例
    EBPFReceiver &instance1 = EBPFReceiver::getInstance();
    EBPFReceiver &instance2 = EBPFReceiver::getInstance();
    EXPECT_EQ(&instance1, &instance2);
}