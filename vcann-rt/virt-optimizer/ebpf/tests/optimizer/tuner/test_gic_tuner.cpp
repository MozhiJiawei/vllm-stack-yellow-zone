/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/gic_tuner.h"

void Clean_mock()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(GICTunerTest, NameTest)
{
    GICTuner tuner;
    EXPECT_EQ(tuner.name(), "GICV4.1 Configuration");
}

TEST(GICTunerTest, GetCategoryTest)
{
    GICTuner tuner;
    EXPECT_EQ(tuner.category(), "IRQ ANOMALY");
}

TEST(GICTunerTest, PrincipleTest)
{
    GICTuner tuner;
    EXPECT_EQ(tuner.principle(),
              "The overhead caused by virtualized interrupts leads to slow processing of interrupt messages by the "
              "kworker process, resulting in long operator dispatch latency and CPU-side bubbles."
              "(Kunpeng 920 only supports GICv3. Kunpeng 920B/920C can additionally support GICv4.1.)");
}

TEST(GICTunerTest, AdviceTest)
{
    GICTuner tuner;
    EXPECT_EQ(
        tuner.advice(),
        "Enbable GIC v4.1 configuration. Support direct injection of virtual interrupts and interrupt pass-through "
        "for vSGIs, eliminating the need for VM exit/entry, which can significantly reduce interrupt response "
        "latency and improve throughput for network/IO-intensive workloads.");
}

TEST(GICTunerTest, CheckWithGICEnabledTokenNotExistAllTest)
{
    GICTuner tuner;
    std::string output = "other_params";

    MOCKER(&CmdExecutor::runCommand).stubs().will(returnValue(std::make_pair(true, output)));

    EXPECT_TRUE(tuner.check());
    Clean_mock();
}

TEST(GICTunerTest, CheckWithGICDisabledTest)
{
    GICTuner tuner;
    std::string output = "other_params";

    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(false, output)));

    EXPECT_TRUE(tuner.check());
    Clean_mock();
}

TEST(GICTunerTest, CheckWithGICEnabledTokenExistAllTest)
{
    GICTuner tuner;
    std::string output = "kvm-arm.vgic_v4_enable=1 other_params";

    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_FALSE(tuner.check());
    Clean_mock();
}

TEST(GICTunerTest, ApplyTest)
{
    GICTuner tuner;
    std::stringstream ss;
    std::streambuf *originalCout = std::cout.rdbuf(ss.rdbuf());

    tuner.apply();

    std::string output = ss.str();
    std::cout.rdbuf(originalCout);

    const std::string expectedOutput =
        "1. Navigate to 'BIOS->Advanced->MISC Config->Support SMMU' and set 'Support SMMU' to 'Enabled'.\n"
        "2. Go to 'BIOS->Advanced->Processor Configuration->GIC Version' and set 'GIC Version' to '4.1'.\n"
        "3. Add the parameter 'kvm-arm.vgic_v4_enable=1' to the system boot configuration file 'grub.cfg' of "
        "the Host OS.\n"
        "4. After rebooting, check the host's dmesg output with 'dmesg|grep GIC'. The following display "
        "indicates that GIC v4.1 has been successfully enabled: 'kvm [1]: GICv4.1 support enabled'.\n"
        "5. On the guest, the following dmesg output indicates that vSGI passthrough is enabled: 'Enabling "
        "SGIs without active state'. \n";
    EXPECT_EQ(output, expectedOutput);
}