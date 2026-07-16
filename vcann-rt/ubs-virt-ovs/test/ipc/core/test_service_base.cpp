/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "test_service_base.h"

#include "protocol.h"
namespace ovs::ut {

std::string TestServiceBase::Name() const
{
    return "TestService";
}

IpcResponse TestServiceBase::FoolHandler(const std::string &req)
{
    lastPayload = req;
    return Ok(BaseResponse::Success());
}

TEST(ServiceTest, RegisterAndHandleMethod)
{
    TestServiceBase svc;

    svc.Register("Foo", &TestServiceBase::FoolHandler);
    std::string payload = "hello";
    IpcResponse resp = svc.Handle("Foo", payload);
    EXPECT_EQ(resp.code_, 0);
    EXPECT_EQ(svc.lastPayload, payload);

    IpcResponse resp2 = svc.Handle("Bar", payload);
    EXPECT_EQ(resp2.code_, static_cast<uint32_t>(VirtIPCCode::METHOD_NOT_FOUND));
}

TEST(ServiceRegistryTest, RegisterAndGetService)
{
    auto &registry = ServiceRegistry::Instance();
    EXPECT_EQ(nullptr, registry.GetService("TestService"));

    auto svc = std::make_shared<TestServiceBase>();
    registry.RegisterService(svc);

    registry.RegisterService(svc);

    auto gotSvc = registry.GetService("TestService");
    ASSERT_NE(gotSvc, nullptr);
    EXPECT_EQ(gotSvc->Name(), "TestService");

    auto missing = registry.GetService("NotExist");
    EXPECT_EQ(missing, nullptr);
}
} // namespace ovs::ut