/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/cluster_topo_tuner.h"

void Mock_Clean_Cluster_Topo_Tuner()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(ClusterTopoTunerTest, NameTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    EXPECT_EQ("Cluster Scheduling Optimization", tuner->name());
}

TEST(ClusterTopoTunerTest, CategoryTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    EXPECT_EQ("CPU BOUND", tuner->category());
}

TEST(ClusterTopoTunerTest, PrincipleTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    EXPECT_EQ("Cross-cluster CPU scheduling leads to slow card issues, multiple cards waiting for the slow card.",
              tuner->principle());
}

TEST(ClusterTopoTunerTest, AdviceTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    EXPECT_EQ("Enable cluster scheduling optimization. Map the physical CPU cluster topology to virtual machines to "
              "achieve the same optimization effects within the virtual machine system as those of the physical CPU.",
              tuner->advice());
}

TEST(ClusterTopoTunerTest, CheckFalseTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    std::string cmdOutput = "domain2";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(true, cmdOutput)));
    EXPECT_EQ(false, tuner->check());
    Mock_Clean_Cluster_Topo_Tuner();
}

TEST(ClusterTopoTunerTest, CheckFalseErrorTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    std::string cmdOutput = "error";
    MOCKER(&CmdExecutor::runCommand).stubs().with(any()).will(returnValue(std::make_pair(false, cmdOutput)));
    EXPECT_EQ(true, tuner->check());
    Mock_Clean_Cluster_Topo_Tuner();
}

TEST(ClusterTopoTunerTest, ApplyTest)
{
    std::shared_ptr<ClusterTopoTuner> tuner = std::make_shared<ClusterTopoTuner>();
    testing::internal::CaptureStdout();
    tuner->apply();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Please enable cluster scheduling optimization."), std::string::npos);
}
