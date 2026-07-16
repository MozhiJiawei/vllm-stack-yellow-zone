/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <mockcpp/GlobalMockObject.h>
#include <mockcpp/mockcpp.hpp>

#include "common/cmd_executor.h"
#include "optimizer/tuner/huge_page_tuner.h"

using namespace std::string_literals;
using namespace testing;

void Mock_Hugepage_Clean()
{
    mockcpp::GlobalMockObject::verify();
    mockcpp::GlobalMockObject::reset();
}

TEST(HugePageTunerTest, GetName)
{
    HugePageTuner tuner;
    EXPECT_EQ(tuner.name(), "Huge Page Configuration");
}

TEST(HugePageTunerTest, GetCategory)
{
    HugePageTuner tuner;
    EXPECT_EQ(tuner.category(), "IO BOUND");
}

TEST(HugePageTunerTest, GetPrinciple)
{
    HugePageTuner tuner;
    EXPECT_EQ(tuner.principle(), "A large number of tlbmiss messages exist in the large-memory VM scenario.");
}

TEST(HugePageTunerTest, GetAdvice)
{
    HugePageTuner tuner;
    EXPECT_EQ(tuner.advice(), "Enable 2M huge page for business operations that use more than 2GB of memory.");
}

TEST(HugePageTunerTest, CheckWithHugePagesTotalZero)
{
    HugePageTuner tuner;

    const char *cmd = "grep -E \"Hugepagesize|HugePages_Total\" /proc/meminfo";
    std::string output = "HugePages_Total: 0\nHugepagesize: 2048\n";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_TRUE(tuner.check());
    Mock_Hugepage_Clean();
}

TEST(HugePageTunerTest, CheckWithHugepagesizeMismatch)
{
    HugePageTuner tuner;

    const char *cmd = "grep -E \"Hugepagesize|HugePages_Total\" /proc/meminfo";
    std::string output = "HugePages_Total: 100\nHugepagesize: 4096\n";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_TRUE(tuner.check());
    Mock_Hugepage_Clean();
}

TEST(HugePageTunerTest, CheckWithHugepagesizeMatch)
{
    HugePageTuner tuner;

    const char *cmd = "grep -E \"Hugepagesize|HugePages_Total\" /proc/meminfo";
    std::string output = "HugePages_Total: 100\nHugepagesize: 2048\n";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, output)));

    EXPECT_FALSE(tuner.check());
    Mock_Hugepage_Clean();
}

TEST(HugePageTunerTest, CheckCommandFailure)
{
    HugePageTuner tuner;
    const char *cmd = "grep -E \"Hugepagesize|HugePages_Total\" /proc/meminfo";
    std::string str = "";
    MOCKER(&CmdExecutor::runCommand).expects(once()).will(returnValue(std::make_pair(true, str)));

    EXPECT_TRUE(tuner.check());
    Mock_Hugepage_Clean();
}

TEST(HugePageTunerTest, ApplyAdvice)
{
    HugePageTuner tuner;
    std::stringstream ss;
    std::streambuf *originalCout = std::cout.rdbuf(ss.rdbuf());

    tuner.apply();

    std::string output = ss.str();
    std::cout.rdbuf(originalCout);

    const std::string expectedOutput =
        "1. Execute the command 'vim /etc/default/grub' and append 'default_hugepagesz=2M hugepagesz=2M "
        "hugepages=pageNums' at the end of the 'GRUB_CMDLINE_LINUX' item, where pageNums should be customized to "
        "the desired number of large pages to be allocated.\n"
        "2. Check whether the virtual machine boots with UEFI or BIOS. If it boots with UEFI, execute "
        "'grub2-mkconfig -o /boot/efi/EFI/openEuler/grub.cfg' (this command is only applicable to openEuler)"
        " to make the modified configuration effective; "
        "if it boots with BIOS, execute 'grub2-mkconfig -o /boot/grub2/grub.cfg' to make the modified "
        "configuration effective.\n"
        "3. Restart the virtual machine, and after restarting, check if the 2M large pages are configured "
        "effectively by running 'cat /proc/meminfo | grep Hugepagesize'.\n"
        "4. During inference, add the environment variable 'export GLIBC_TUNABLES=glibc.malloc.hugetlb=2' "
        "to enable the optimization of 2M large pages for inference. \n";

    EXPECT_EQ(output, expectedOutput);
}