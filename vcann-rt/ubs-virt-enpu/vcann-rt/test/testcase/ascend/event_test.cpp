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

#include <acl/acl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <atomic>
#include <mockcpp/mockcpp.hpp>
#include "core_limiter.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "rts_event.h"
#include "runtime_stub.h"
#include "securec.h"

using namespace testing;

class EventTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Event test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Event test end" << std::endl;
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

TEST_F(EventTest, rtEventDestroySync)
{
    rtEvent_t event = nullptr;
    rtError_t ret = rtEventDestroySync(event);
    EXPECT_EQ(ret, RT_ERROR_NONE);
}

TEST_F(EventTest, rtEventCreateTest)
{
    rtEvent_t event;
    rtError_t ret = rtEventCreate(&event);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsEventCreateTest)
{
    rtEvent_t event;
    uint64_t flag = 0;
    rtError_t ret = rtsEventCreate(&event, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsEventCreateExTest)
{
    rtEvent_t event;
    uint64_t flag = 0;
    rtError_t ret = rtsEventCreateEx(&event, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtEventCreateWithFlagTest)
{
    rtEvent_t event;
    uint64_t flag = 0;
    rtError_t ret = rtEventCreateWithFlag(&event, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtEventCreateExWithFlagTest)
{
    rtEvent_t event;
    uint64_t flag = 0;
    rtError_t ret = rtEventCreateExWithFlag(&event, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtStreamWaitEventTest)
{
    rtEvent_t event = nullptr;
    rtStream_t stream = nullptr;
    rtError_t ret = rtStreamWaitEvent(stream, event);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtEventRecordTest)
{
    rtEvent_t event = nullptr;
    rtStream_t stream = nullptr;
    MOCKER(set_event_record_status).stubs().with(mockcpp::any(), mockcpp::any());
    rtError_t ret = rtEventRecord(event, stream);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtEventDestroyTest)
{
    rtEvent_t event = nullptr;
    MOCKER(set_event_destroy_status).stubs().with(mockcpp::any());
    rtError_t ret = rtEventDestroy(event);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtStreamDestroyTest)
{
    rtStream_t stream = nullptr;
    MOCKER(core_limiter).stubs().with(mockcpp::any(), mockcpp::any(), mockcpp::any());
    rtError_t ret = rtStreamDestroy(stream);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtDestroyStreamForceTest)
{
    rtStream_t stream = nullptr;
    MOCKER(core_limiter).stubs().with(mockcpp::any(), mockcpp::any(), mockcpp::any());
    rtError_t ret = rtDestroyStreamForce(stream);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsNotifyCreateTest)
{
    rtNotify_t notify = nullptr;
    uint64_t flag = 0;
    MOCKER(set_event_create_status).stubs().with(mockcpp::any());
    rtError_t ret = rtsNotifyCreate(&notify, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtNotifyRecordTest)
{
    rtNotify_t notify = nullptr;
    rtStream_t stm = nullptr;
    rtError_t ret = rtNotifyRecord(notify, stm);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtNotifyDestroyTest)
{
    rtNotify_t notify = nullptr;
    rtError_t ret = rtNotifyDestroy(notify);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsNotifyWaitAndResetTest)
{
    rtNotify_t notify = nullptr;
    rtStream_t stm = nullptr;
    uint32_t timeout = 0;
    rtError_t ret = rtsNotifyWaitAndReset(notify, stm, timeout);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtStreamWaitEventWithTimeoutTest)
{
    rtStream_t stm = nullptr;
    rtEvent_t evt = nullptr;
    uint32_t timeout = 0;
    rtError_t ret = rtStreamWaitEventWithTimeout(stm, evt, timeout);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtNotifyWaitTest)
{
    rtNotify_t notify = nullptr;
    rtStream_t stm = nullptr;
    rtError_t ret = rtNotifyWait(notify, stm);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtNotifyWaitWithTimeOutTest)
{
    rtNotify_t notify = nullptr;
    rtStream_t stm = nullptr;
    uint32_t timeout = 0;
    rtError_t ret = rtNotifyWaitWithTimeOut(notify, stm, timeout);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtNotifyCreateWithFlagTest)
{
    int32_t deviceId = 0;
    rtNotify_t notify = nullptr;
    uint32_t flag = 0;
    rtError_t ret = rtNotifyCreateWithFlag(deviceId, &notify, flag);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtCntNotifyCreate)
{
    int32_t deviceId = 0;
    rtCntNotify_t cntNotify = nullptr;
    rtError_t ret = rtCntNotifyCreate(deviceId, &cntNotify);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtCntNotifyCreateWithFlag)
{
    int32_t deviceId = 0;
    rtCntNotify_t cntNotify = nullptr;
    uint32_t flags = 0;
    rtError_t ret = rtCntNotifyCreateWithFlag(deviceId, &cntNotify, flags);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtCntNotifyRecord)
{
    rtCntNotify_t inCntNotify = nullptr;
    rtStream_t stm = nullptr;
    rtCntNtyRecordInfo_t info = nullptr;
    rtError_t ret = rtCntNotifyRecord(inCntNotify, stm, &info);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtCntNotifyWaitWithTimeout)
{
    rtCntNotify_t inCntNotify = nullptr;
    rtStream_t stm = nullptr;
    rtCntNtyWaitInfo_t info = nullptr;
    rtError_t ret = rtCntNotifyWaitWithTimeout(inCntNotify, stm, &info);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtCntNotifyDestroy)
{
    rtCntNotify_t inCntNotify = nullptr;
    rtError_t ret = rtCntNotifyDestroy(inCntNotify);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsCntNotifyRecord)
{
    rtCntNotify_t cntNotify = nullptr;
    rtStream_t stm = nullptr;
    rtCntNotifyRecordInfo_t info = nullptr;
    rtError_t ret = rtsCntNotifyRecord(cntNotify, stm, &info);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}

TEST_F(EventTest, rtsCntNotifyWaitWithTimeout)
{
    rtCntNotify_t cntNotify = nullptr;
    rtStream_t stm = nullptr;
    rtCntNotifyWaitInfo_t info = nullptr;
    rtError_t ret = rtsCntNotifyWaitWithTimeout(cntNotify, stm, &info);
    EXPECT_EQ(ret, ACL_RT_SUCCESS);
}
