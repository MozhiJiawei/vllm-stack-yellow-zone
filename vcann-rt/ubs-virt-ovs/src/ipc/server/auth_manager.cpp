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

#include "auth_manager.h"
#include "logger.h"

#include <sstream>
#include <string_view>

namespace virt::ovs::ipc::server {

bool AuthManager::AuthorizeUser(const std::string &username, std::string &authority, config::ConfigModule &conf)
{
    auto ret = conf.GetConf("auth", username, authority);
    if (ret != config::ConfigCode::OK) {
        LOG_ERROR << "Get auth conf failed, ret=" << static_cast<uint32_t>(ret);
        return false;
    }
    LOG_INFO << "Get auth conf success, username=" << username << " authority=" << authority;
    return true;
}

bool AuthManager::AuthorizeService(const std::string &authority, const std::string &serviceKey)
{
    auto trimSpace = [](std::string_view v) {
        auto begin = v.find_first_not_of(' ');
        if (begin == std::string_view::npos) {
            return std::string_view{};
        }
        auto end = v.find_last_not_of(' ');
        return v.substr(begin, end - begin + 1);
    };

    std::string_view keyv = trimSpace(serviceKey);
    std::stringstream ss(authority);
    std::string item;

    while (std::getline(ss, item, ',')) {
        if (trimSpace(item) == keyv) {
            return true;
        }
    }
    return false;
}

} // namespace virt::ovs::ipc::server