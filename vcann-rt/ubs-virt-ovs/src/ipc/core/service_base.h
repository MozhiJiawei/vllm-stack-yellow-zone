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

#ifndef UBS_VIRT_OVS_SERVICE_H
#define UBS_VIRT_OVS_SERVICE_H

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "virt_msg.h"

namespace virt::ovs {

class Service {
public:
    using HandlerFunc = std::function<msg::IpcResponse(const std::string &)>;

    virtual ~Service() = default;
    virtual std::string Name() const = 0;

    void RegisterMethod(const std::string &method, HandlerFunc handler);

    msg::IpcResponse Handle(const std::string &method, const std::string &reqPayload) const;

protected:
    template <typename T>
    void Register(const std::string &method, msg::IpcResponse (T::*func)(const std::string &))
    {
        RegisterMethod(method, [this, func](const std::string &reqPayload) {
            return (static_cast<T *>(this)->*func)(reqPayload);
        });
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, HandlerFunc> handlers_;
};

class ServiceRegistry {
public:
    static ServiceRegistry &Instance();
    void RegisterService(const std::shared_ptr<Service> &svc);
    std::shared_ptr<Service> GetService(const std::string &serviceName);

private:
    ServiceRegistry() = default;
    std::unordered_map<std::string, std::shared_ptr<Service>> services_;
    std::mutex mutex_;
};

} // namespace virt::ovs

#endif // UBS_VIRT_OVS_SERVICE_H