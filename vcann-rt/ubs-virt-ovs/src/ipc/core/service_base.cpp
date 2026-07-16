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

#include "service_base.h"
#include "logger.h"
#include "protocol.h"

namespace virt::ovs {
void Service::RegisterMethod(const std::string &method, HandlerFunc handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[method] = std::move(handler);
    LOG_INFO << "Registered method: " << Name() << "." << method;
}

IpcResponse Service::Handle(const std::string &method, const std::string &reqPayload) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
        LOG_ERROR << "Method " << method << " not registered.";
        return IpcError(VirtIPCCode::METHOD_NOT_FOUND);
    }
    LOG_INFO << "Dispatch method: " << Name() << "." << method;
    return it->second(reqPayload);
}

ServiceRegistry &ServiceRegistry::Instance()
{
    static ServiceRegistry instance;
    return instance;
}

void ServiceRegistry::RegisterService(const std::shared_ptr<Service> &svc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &name = svc->Name();
    if (services_.count(name)) {
        LOG_WARN << "Service " << name << " already registered.";
        return;
    }
    LOG_INFO << "Registering service " << name;
    services_[svc->Name()] = svc;
}

std::shared_ptr<Service> ServiceRegistry::GetService(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(name);
    if (it == services_.end()) {
        LOG_ERROR << "service " << name << " does not exist.";
        return nullptr;
    }
    return it->second;
}

} // namespace virt::ovs