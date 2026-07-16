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
#include "test_vas_cli_process.h"

#include "mockcpp/mokc.h"

#include "cpu_helper.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "vas_cli_parse.h"
#include "vas_cli_process_ctl.h"
#include "vas_cli_res_echo.h"
#include "vasctl_arg_parse.h"

using namespace vas::cli::framework;
using namespace vas::common;
using namespace vas::cli::reg;
namespace vas::ut::cli {
void TestVasCliProcess::SetUp()
{
    Test::SetUp();
}

void TestVasCliProcess::TearDown()
{
    VasCliParse::GetInstance().needPrtHelp = false;
    GlobalMockObject::verify();
    Test::TearDown();
}

VasRet MockExecuteCommandVasOk()
{
    return VAS_OK;
}

VasRet MockExecuteCommandVasError()
{
    return VAS_ERROR;
}

TEST_F(TestVasCliProcess, testMainExecuteProcessSetConfigTest)
{
    RegisterCliModuleSDK();

    char arg1[] = "./vasctl";
    char arg2[] = "set";
    char arg3[] = "config";
    char arg4[] = "-sp";
    char arg5[] = "bind";

    char *testArgs[] = {arg1, arg2, arg3, arg4, arg5};
    int testArgc = 5;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testMainExecuteQueryAffinityAll)
{
    RegisterCliModuleSDK();

    char arg1[] = "./vasctl";
    char arg2[] = "query";
    char arg3[] = "affinity";
    char arg4[] = "-s";
    char arg5[] = "all";

    char *testArgs[] = {arg1, arg2, arg3, arg4, arg5};
    int testArgc = 5;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testMainExecuteProcessWithHelp)
{
    RegisterCliModuleSDK();

    char arg1[] = "./vasctl";

    char *testArgs[] = {arg1};
    int testArgc = 1;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_ERROR_CMD);
}

TEST_F(TestVasCliProcess, testMainExecuteOptReassign)
{
    RegisterCliModuleSDK();

    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "reassign";
    char arg4[] = "-s";
    char arg5[] = "all";

    char *testArgs[] = {arg1, arg2, arg3, arg4, arg5};
    int testArgc = 5;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_ERROR);
}

TEST_F(TestVasCliProcess, testMainExecuteOptReassign2)
{
    RegisterCliModuleSDK();

    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "reassign";

    char *testArgs[] = {arg1, arg2, arg3};
    int testArgc = 3;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testCliOptRecoverCliFunc)
{
    RegisterCliModuleSDK();
    MOCKER(&Logger::Init).stubs().will(invoke(MockExecuteCommandVasOk)).then(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::CpuHelper::Init).stubs().will(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::LibvirtHelper::Init).stubs().will(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::LibvirtHelper::GetVmInfoList).stubs().will(invoke(MockExecuteCommandVasOk));
    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "recover";

    char *testArgs[] = {arg1, arg2, arg3};
    int testArgc = 3;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testCliOptRecoverFailFunc)
{
    RegisterCliModuleSDK();
    MOCKER(&Logger::Init).stubs().will(invoke(MockExecuteCommandVasOk)).then(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::CpuHelper::Init).stubs().will(invoke(MockExecuteCommandVasError));
    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "recover";

    char *testArgs[] = {arg1, arg2, arg3};
    int testArgc = 3;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testCliOptRecoverFailFunc3)
{
    RegisterCliModuleSDK();
    MOCKER(&Logger::Init).stubs().will(invoke(MockExecuteCommandVasOk)).then(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::CpuHelper::Init).stubs().will(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::LibvirtHelper::Init).stubs().will(invoke(MockExecuteCommandVasError));
    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "recover";

    char *testArgs[] = {arg1, arg2, arg3};
    int testArgc = 3;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

TEST_F(TestVasCliProcess, testCliOptRecoverFailFunc4)
{
    RegisterCliModuleSDK();
    MOCKER(&Logger::Init).stubs().will(invoke(MockExecuteCommandVasOk)).then(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::CpuHelper::Init).stubs().will(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::LibvirtHelper::Init).stubs().will(invoke(MockExecuteCommandVasOk));
    MOCKER(&vas::sched::acquire::LibvirtHelper::GetVmInfoList).stubs().will(invoke(MockExecuteCommandVasError));
    char arg1[] = "./vasctl";
    char arg2[] = "opt";
    char arg3[] = "recover";

    char *testArgs[] = {arg1, arg2, arg3};
    int testArgc = 3;

    testing::internal::CaptureStdout();
    VasCliProcessCtl processCtl;
    VasRet result = processCtl.MainExecuteProcess(testArgc, testArgs);
    std::string output = testing::internal::GetCapturedStdout();

    std::cerr << "result: " << result << std::endl;
    std::cerr << "Captured output: " << output << std::endl;
    EXPECT_EQ(result, VAS_OK);
}

} // namespace vas::ut::cli