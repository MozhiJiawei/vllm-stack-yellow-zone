/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "cmd_executor.h"
#include "optimizer/tuner/cpu_topo_tuner.h"

void Mock_Clean_Cpu_Topo_Tuner()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(CPUTopoTunerTest, NameTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    EXPECT_EQ("CPU Core Binding", tuner->name());
}

TEST(CPUTopoTunerTest, CategoryTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    EXPECT_EQ("CPU BOUND", tuner->category());
}

TEST(CPUTopoTunerTest, PrincipleTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    EXPECT_EQ(
        "The topology structure does not match the physical machine, resulting in high scheduling and memory access "
        "overhead.",
        tuner->principle());
}

TEST(CPUTopoTunerTest, AdviceTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    EXPECT_EQ(
        "Enable CPU core binding. Synchronize the topology of physical machine and virtual machine in pass-through "
        "scenarios.",
        tuner->advice());
}

TEST(CPUTopoTunerTest, CheckCmdFailureTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckUnknownFormatTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU: unknown\nCPU: 0";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest1)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU: 0\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest2)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU:-1\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest3)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU:1\nCPU:0\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest4)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "CPU:0\n";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest5)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU:1\nCPU:0\nVCPU:0\nCPU:0";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest6)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU:1\nCPU:-1";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, CheckTest7)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    std::string cmdOutput = "VCPU:1\nVCPU:2";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cpu_Topo_Tuner();
}

TEST(CPUTopoTunerTest, ApplyTest)
{
    std::shared_ptr<CPUTopoTuner> tuner = std::make_shared<CPUTopoTuner>();
    tuner->apply();
}