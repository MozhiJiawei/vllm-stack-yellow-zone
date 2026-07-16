/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <fstream>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/npu_topo_tuner.h"

class NPUTopoTunerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tuner = std::make_unique<NPUTopoTuner>();
    }

    std::unique_ptr<NPUTopoTuner> tuner;
};

TEST_F(NPUTopoTunerTest, TestName)
{
    EXPECT_EQ(tuner->name(), "NUMA NPU Binding");
}

TEST_F(NPUTopoTunerTest, TestCategory)
{
    EXPECT_EQ(tuner->category(), "CPU BOUND");
}

TEST_F(NPUTopoTunerTest, TestPrinciple)
{
    EXPECT_EQ(tuner->principle(), "The lack of affinity between the NPU and "
                                  "CPU leads to high scheduling and memory access overhead.");
}

TEST_F(NPUTopoTunerTest, TestAdvice)
{
    EXPECT_EQ(tuner->advice(), "Enable NUMA NPU binding. Synchronize the NPU NUMA information to the virtual machine.");
}

TEST_F(NPUTopoTunerTest, TestCheckCase1)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": 12,\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    EXPECT_TRUE(tuner->check());
    EXPECT_FALSE(tuner->isLastCheckSuccess);
}

TEST_F(NPUTopoTunerTest, TestCheckCase2)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d804\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    EXPECT_TRUE(tuner->check());
    EXPECT_FALSE(tuner->isLastCheckSuccess);
}

TEST_F(NPUTopoTunerTest, TestCheckCase3)
{
    std::string filename = "/usr/local/sbin/ubs-optimizer/config.json";
    std::ofstream outfile(filename);
    outfile << "{\"sampling_interval\": 30,\n"
               "\"bind_port\": 10101,\n"
               "\"vm_name\": \"openeuler\",\n"
               "\"npu_type\": \"d802\",\n"
               "\"system\" : {}}"
            << std::endl;
    outfile.close();
    std::string str = "check";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, str)));
    EXPECT_FALSE(tuner->check());
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST_F(NPUTopoTunerTest, TestCheckMultipleNPUsSomeNoNUMAFlag)
{
    EXPECT_FALSE(tuner->check());
}

TEST_F(NPUTopoTunerTest, TestApply)
{
    EXPECT_NO_THROW(tuner->apply());
}