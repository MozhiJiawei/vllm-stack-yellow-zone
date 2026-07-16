/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "server/collector/qemu_collector.h"

void QEMUClean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(QEMUCollectorTest, SplitBySpaceSuccess)
{
    std::string line = "VM/task-name  P  1  test_vm  2";
    QEMUCollector &collector = QEMUCollector::getInstance();
    std::vector<std::string> tokens = collector.splitBySpace(line);

    EXPECT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0], "VM/task-name");
    EXPECT_EQ(tokens[1], "P");
    EXPECT_EQ(tokens[2], "1");
    EXPECT_EQ(tokens[3], "test_vm");
    EXPECT_EQ(tokens[4], "2");
    QEMUClean();
}

TEST(QEMUCollectorTest, FindPIndexSuccess)
{
    std::vector<std::string> tokens = {"VM/task-name", "P", "1", "test_vm", "2"};
    QEMUCollector &collector = QEMUCollector::getInstance();
    int index = collector.findPIndex(tokens);

    EXPECT_EQ(index, 1);
    QEMUClean();
}

TEST(QEMUCollectorTest, FindPIndexFail)
{
    std::vector<std::string> tokens = {"VM/task-name", "1", "test_vm", "2"};
    QEMUCollector &collector = QEMUCollector::getInstance();
    int index = collector.findPIndex(tokens);

    EXPECT_EQ(index, -1);
    QEMUClean();
}

TEST(QEMUCollectorTest, MonitorQemuThreadsSuccess)
{
    std::string vm_name = "openeuler";
    double timeout = 20;
    std::atomic<int> mv_count(-1);
    QEMUCollector &collector = QEMUCollector::getInstance();

    // 使用echo模拟 vmtop的回显，打印四行数据
    std::string data = "\"VM/task-name P\nopeneuler 100\nopeneuler 200\n xxxxx\"";
    std::string cmd = "echo -e " + data;
    FILE *pipe = popen(cmd.c_str(), "r");

    auto start_time = std::chrono::system_clock::now();
    auto end_less_timeout_time = start_time + std::chrono::seconds(10);
    auto end_more_timeout_time = start_time + std::chrono::seconds(30);

    // mock时间函数，以保证值被正确刷新
    MOCKER(std::chrono::system_clock::now)
        .stubs()
        .with(any())
        .will(returnValue(start_time))
        .then(returnValue(end_less_timeout_time))
        .then(returnValue(end_more_timeout_time));
    MOCKER(popen).stubs().with(any()).will(returnValue(pipe));
    MOCKER(pclose).stubs().with(any()).will(returnValue(0));

    collector.monitor_qemu_threads(vm_name, timeout, mv_count);
    int res;
    res = mv_count.load(std::memory_order_relaxed);

    EXPECT_EQ(res, 1);
    QEMUClean();
}
