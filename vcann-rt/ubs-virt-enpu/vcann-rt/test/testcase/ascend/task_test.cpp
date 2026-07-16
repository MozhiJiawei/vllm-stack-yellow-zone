/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <mockcpp/mockcpp.hpp>
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"

class TaskTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Task test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Task test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755); // ut中的memctl.lock文件,设置为755权限
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
    }

    void TearDown()
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
        close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

TEST_F(TaskTest, rtFftsPlusTaskLaunchTest)
{
    rtFftsPlusTaskInfo_t *fftsPlusTaskInfo = nullptr;
    rtStream_t stm = nullptr;
    rtError_t ret = rtFftsPlusTaskLaunch(fftsPlusTaskInfo, stm);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtFftsPlusTaskLaunchWithFlagTest)
{
    rtFftsPlusTaskInfo_t *fftsPlusTaskInfo = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtFftsPlusTaskLaunchWithFlag(fftsPlusTaskInfo, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtFftsTaskLaunchTest)
{
    rtFftsTaskInfo_t *fftsTaskInfo = nullptr;
    rtStream_t stm = nullptr;
    rtError_t ret = rtFftsTaskLaunch(fftsTaskInfo, stm);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtFftsTaskLaunchWithFlagTest)
{
    rtFftsTaskInfo_t *fftsTaskInfo = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtFftsTaskLaunchWithFlag(fftsTaskInfo, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtStarsTaskLaunchTest)
{
    void *taskSqe = nullptr;
    uint32_t sqeLen = 0;
    rtStream_t stm = nullptr;
    rtError_t ret = rtStarsTaskLaunch(taskSqe, sqeLen, stm);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtStarsTaskLaunchWithFlagTest)
{
    void *taskSqe = nullptr;
    uint32_t sqeLen = 0;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtStarsTaskLaunchWithFlag(taskSqe, sqeLen, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtCmoTaskLaunchTest)
{
    rtCmoTaskInfo_t *taskInfo = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtCmoTaskLaunch(taskInfo, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtCmoAddrTaskLaunchTest)
{
    void *cmoAddrInfo = nullptr;
    uint64_t destMax = 0;
    rtCmoOpCode_t cmoOpCode = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtCmoAddrTaskLaunch(cmoAddrInfo, destMax, cmoOpCode, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtBarrierTaskLaunchTest)
{
    rtBarrierTaskInfo_t *taskInfo = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtBarrierTaskLaunch(taskInfo, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtMultipleTaskInfoLaunchTest)
{
    rtBarrierTaskInfo_t *taskInfo = nullptr;
    rtStream_t stm = nullptr;
    rtError_t ret = rtMultipleTaskInfoLaunch(taskInfo, stm);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(TaskTest, rtMultipleTaskInfoLaunchWithFlagTest)
{
    void *taskInfo = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtMultipleTaskInfoLaunchWithFlag(taskInfo, stm, flag);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}
