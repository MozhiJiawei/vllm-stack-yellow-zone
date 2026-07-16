/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/write_combine_tuner.h"

using namespace std::string_literals;
using namespace testing;

void Clean_Write_Mock()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(WriteCombineTunerTest, GetName)
{
    WriteCombineTuner tuner;
    EXPECT_EQ(tuner.name(), "Write Combine");
}

TEST(WriteCombineTunerTest, GetCategory)
{
    WriteCombineTuner tuner;
    EXPECT_EQ(tuner.category(), "IO BOUND");
}

TEST(WriteCombineTunerTest, GetPrinciple)
{
    WriteCombineTuner tuner;
    EXPECT_EQ(
        tuner.principle(),
        "The physical machine supports small-packet PCIe BAR space copying, but the virtual machine does not. As a "
        "result, asynchronous DMA copying is used, which takes longer and leads to degraded H2D/D2H small-packet "
        "data copy performance.");
}

TEST(WriteCombineTunerTest, GetAdvice)
{
    WriteCombineTuner tuner;
    EXPECT_EQ(tuner.advice(),
              "Enable write combine. Data written to the WC region is temporarily stored in a 64-byte buffer. When the "
              "buffer is full or a refresh event is triggered, a merge write is executed to enhance throughput.");
}

TEST(WriteCombineTunerTest, CheckWithTargetFlagFound)
{
    WriteCombineTuner tuner;

    const char *cmd = "cd /tmp && msnpureport && grep -nr \"Device capability info\" "
                      "/tmp/2023-10-24-10* && rm -rf 2023-10-24-10*";
    std::string output = "Device capability info: feature_bar_mem=1\n";

    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_FALSE(tuner.check());
    Clean_Write_Mock();
}

TEST(WriteCombineTunerTest, CheckWithTargetFlagNotFound)
{
    WriteCombineTuner tuner;

    const char *cmd = "cd /tmp && msnpureport && grep -nr \"Device capability info\" "
                      "/tmp/2023-10-24-10* && rm -rf 2023-10-24-10*";
    std::string output = "Device capability info: other_feature=1\n";

    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_TRUE(tuner.check());
    Clean_Write_Mock();
}

TEST(WriteCombineTunerTest, CheckCommandFailure)
{
    WriteCombineTuner tuner;

    const char *cmd = "cd /tmp && msnpureport && grep -nr \"Device capability info\" "
                      "/tmp/2023-10-24-10* && rm -rf 2023-10-24-10*";
    std::string output = "";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_TRUE(tuner.check());
    Clean_Write_Mock();
}

TEST(WriteCombineTunerTest, ApplyAdvice)
{
    WriteCombineTuner tuner;
    std::stringstream ss;
    std::streambuf *originalCout = std::cout.rdbuf(ss.rdbuf());

    tuner.apply();

    std::string output = ss.str();
    std::cout.rdbuf(originalCout);

    const std::string expectedOutput =
        "1. Please enable the WriteCombine feature within the virtual machine according to the provided PATCH.\n"
        "2. Ensure that the HostOS and Qemu are compatible with the modifications in the PATCH, "
        "and install the latest NPU HDK driver within the virtual machine.\n";

    EXPECT_EQ(output, expectedOutput);
    Clean_Write_Mock();
}