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

#include "test_dispatcher.h"

#include "protocol.h"
#include "virt_msg.h"
#include "virt_msg_packer.h"

using namespace virt::ovs;
using namespace virt::ovs::msg;
using namespace virt::ovs::ipc::server;
namespace ovs::ut {

class MockEchoService : public Service {
public:
    std::string Name() const override
    {
        return "MockEcho";
    }

    MockEchoService()
    {
        Register("Echo", &MockEchoService::EchoHandler);
    }

    std::string lastPayload;

private:
    IpcResponse EchoHandler(const std::string &req)
    {
        lastPayload = req;
        BaseResponse bizResp(VirtIPCCode::OK, "echo:" + req);
        return Ok(bizResp);
    }
};

TEST_F(TestDispatcher, ServiceNotFound)
{
    Dispatcher disp;
    IpcRequest req;
    req.service_ = "NonExistentService";
    req.method_ = "Foo";
    req.payload_ = "data";

    IpcResponse resp = disp.Dispatch(req);
    EXPECT_EQ(resp.code_, static_cast<uint32_t>(VirtIPCCode::SERVICE_NOT_FOUND));
}

TEST_F(TestDispatcher, DispatchToRegisteredService)
{
    auto svc = std::make_shared<MockEchoService>();
    ServiceRegistry::Instance().RegisterService(svc);

    Dispatcher disp;
    IpcRequest req;
    req.service_ = "MockEcho";
    req.method_ = "Echo";
    req.payload_ = "hello";

    IpcResponse resp = disp.Dispatch(req);
    EXPECT_EQ(resp.code_, static_cast<uint32_t>(VirtIPCCode::OK));
    EXPECT_EQ(svc->lastPayload, "hello");
}

TEST_F(TestDispatcher, DispatchMethodNotFound)
{
    auto svc = std::make_shared<MockEchoService>();
    ServiceRegistry::Instance().RegisterService(svc);

    Dispatcher disp;
    IpcRequest req;
    req.service_ = "MockEcho";
    req.method_ = "NonExistentMethod";
    req.payload_ = "data";

    IpcResponse resp = disp.Dispatch(req);
    EXPECT_EQ(resp.code_, static_cast<uint32_t>(VirtIPCCode::METHOD_NOT_FOUND));
}

} // namespace ovs::ut
