/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "test_urma_utility.h"

#include "urma_utility.h"

namespace ovs::ut {
using namespace virt::ovs::ubse::urma;

static int g_urmaFakeHandle = 0;
static int g_minBw = 10;
static int g_maxBw = 100;

static uint32_t MockGetBw(const char *, uint32_t *minBw, uint32_t *maxBw)
{
    if (minBw)
        *minBw = g_minBw;
    if (maxBw)
        *maxBw = g_maxBw;
    return 0;
}
static uint32_t MockSetBw(const char *, uint32_t, uint32_t)
{
    return 0;
}
static uint32_t MockResetBw(const char *)
{
    return 0;
}

template <typename T>
static void *MockDlsym(T handle, const char *symbol)
{
    if (std::string(symbol) == "ubs_urma_bandwidth_get")
        return reinterpret_cast<void *>(MockGetBw);
    if (std::string(symbol) == "ubs_urma_bandwidth_set")
        return reinterpret_cast<void *>(MockSetBw);
    if (std::string(symbol) == "ubs_urma_bandwidth_reset")
        return reinterpret_cast<void *>(MockResetBw);
    return nullptr;
}

void TestUrmaUtility::SetUp()
{
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(&g_urmaFakeHandle)));

    // Instead of mockcpp string matching which is causing issues, map dlsym to a mock function
    MOCKER(dlsym).stubs().will(invoke(MockDlsym<void *>));
}

void TestUrmaUtility::TearDown()
{
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST_F(TestUrmaUtility, GetBandWidth_Success)
{
    UrmaUtility utility;
    uint32_t minBw = 0;
    uint32_t maxBw = 0;
    EXPECT_EQ(utility.GetBandWidth("dev", minBw, maxBw), 0u);
    EXPECT_EQ(minBw, g_minBw);
    EXPECT_EQ(maxBw, g_maxBw);
}

TEST_F(TestUrmaUtility, SetBandWidth_Success)
{
    UrmaUtility utility;
    EXPECT_EQ(utility.SetBandWidth("dev", g_minBw, g_maxBw), 0u);
}

TEST_F(TestUrmaUtility, ResetBandWidth_Success)
{
    UrmaUtility utility;
    EXPECT_EQ(utility.ResetBandWidth("dev"), 0u);
}
TEST_F(TestUrmaUtility, DestructorCleanup)
{
    {
        UrmaUtility utility;
        uint32_t minBw = 0;
        uint32_t maxBw = 0;
        EXPECT_EQ(utility.GetBandWidth("dev", minBw, maxBw), 0u);
        EXPECT_EQ(utility.SetBandWidth("dev", 10, 20), 0u);
        EXPECT_EQ(utility.ResetBandWidth("dev"), 0u);
    }
    SUCCEED();
}

TEST_F(TestUrmaUtility, MultipleOperationsOnSameDevice)
{
    UrmaUtility utility;
    uint32_t minBw = 0;
    uint32_t maxBw = 0;
    EXPECT_EQ(utility.SetBandWidth("dev1", 100, 200), 0u);
    EXPECT_EQ(utility.GetBandWidth("dev1", minBw, maxBw), 0u);
    EXPECT_EQ(utility.ResetBandWidth("dev1"), 0u);
}

} // namespace ovs::ut
