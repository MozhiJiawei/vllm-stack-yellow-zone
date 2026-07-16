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

class GraphTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Graph test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Graph test end" << std::endl;
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

TEST_F(GraphTest, rtModelExecute)
{
    rtModel_t mdl = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t error = rtModelExecute(mdl, stm, flag);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtModelExecuteAsync)
{
    rtModel_t mdl = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    rtError_t error = rtModelExecuteAsync(mdl, stm, flag);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtsModelExecute)
{
    rtModel_t mdl = nullptr;
    int32_t timeout = 0;
    rtError_t error = rtsModelExecute(mdl, timeout);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtModelExecuteSync)
{
    rtModel_t mdl = nullptr;
    rtStream_t stm = nullptr;
    uint32_t flag = 0;
    int32_t timeout = 0;
    rtError_t error = rtModelExecuteSync(mdl, stm, flag, timeout);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtStreamBeginCapture)
{
    rtStream_t stm = nullptr;
    rtStreamCaptureMode mode = RT_STREAM_CAPTURE_MODE_GLOBAL;
    rtError_t error = rtStreamBeginCapture(stm, mode);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtStreamEndCapture)
{
    rtStream_t stm = nullptr;
    rtModel_t *mdl = nullptr;
    rtError_t error = rtStreamEndCapture(stm, mdl);
    EXPECT_EQ(error, RT_ERROR_NONE);
}

TEST_F(GraphTest, rtsModelExecuteAsync)
{
    rtModel_t mdl = nullptr;
    rtStream_t stm = nullptr;
    rtError_t error = rtsModelExecuteAsync(mdl, stm);
    EXPECT_EQ(error, RT_ERROR_NONE);
}
