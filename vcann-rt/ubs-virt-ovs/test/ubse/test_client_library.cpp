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
#include "test_client_library.h"

#include "client_library.h"

namespace ovs::ut {
using namespace virt::ovs::ubse::client;

static int g_fakeHandle = 0;

void TestClientLibrary::SetUp() {}
void TestClientLibrary::TearDown()
{
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST_F(TestClientLibrary, Open_Success)
{
    ClientLibrary lib("/dummy.so");
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(&g_fakeHandle)));

    EXPECT_NO_THROW(lib.Open());
    EXPECT_EQ(lib.handle, &g_fakeHandle);
}

TEST_F(TestClientLibrary, Open_Failed)
{
    ClientLibrary lib("/dummy.so");
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(nullptr)));
    static std::string mockErrStr = "mock error";
    MOCKER(dlerror).stubs().will(returnValue(const_cast<char *>(mockErrStr.c_str())));
    EXPECT_THROW(lib.Open(), std::runtime_error);
}

TEST_F(TestClientLibrary, GetSymbol_Success)
{
    ClientLibrary lib("/dummy.so");
    lib.handle = &g_fakeHandle;

    int fakeSymbol = 0;
    MOCKER(dlsym).stubs().with(any(), any()).will(returnValue(static_cast<void *>(&fakeSymbol)));
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(&g_fakeHandle)));

    void *sym = lib.GetSymbol("test_symbol");
    EXPECT_EQ(sym, &fakeSymbol);
}

TEST_F(TestClientLibrary, GetSymbol_NotFound)
{
    ClientLibrary lib("/dummy.so");
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(&g_fakeHandle)));
    MOCKER(dlsym).stubs().with(any(), any()).will(returnValue(static_cast<void *>(nullptr)));
    static std::string notFoundStr = "not found";
    MOCKER(dlerror).stubs().will(returnValue(const_cast<char *>(notFoundStr.c_str())));

    EXPECT_THROW(lib.GetSymbol("test_symbol"), std::runtime_error);
}
TEST_F(TestClientLibrary, Instance_DefaultPath)
{
    ClientLibrary &lib = ClientLibrary::Instance();
    EXPECT_NE(&lib, nullptr);
}

TEST_F(TestClientLibrary, GetSymbol_NullHandle)
{
    ClientLibrary lib("/dummy.so");
    MOCKER(dlopen).stubs().with(any(), any()).will(returnValue(static_cast<void *>(nullptr)));
    static std::string mockErrStr = "mock error";
    MOCKER(dlerror).stubs().will(returnValue(const_cast<char *>(mockErrStr.c_str())));

    EXPECT_THROW(lib.GetSymbol("test_symbol"), std::runtime_error);
}

} // namespace ovs::ut
