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

#include "dispatcher.h"
#include "ipc/code/virt_ipc_code.h"
#include "ipc/protocol/protocol.h"
#include "logger.h"

namespace virt::ovs::ipc::server {

msg::IpcResponse Dispatcher::Dispatch(const msg::IpcRequest &req) const
{
    LOG_INFO << "request: service=" << req.service_ << ",method=" << req.method_;
    auto svc = ServiceRegistry::Instance().GetService(req.service_);
    if (!svc) {
        LOG_ERROR << "dispatch failed,service not found: " << req.service_;
        return IpcError(VirtIPCCode::SERVICE_NOT_FOUND);
    }
    return svc->Handle(req.method_, req.payload_);
}

} // namespace virt::ovs::ipc::server