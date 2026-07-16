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

#ifndef VIRT_OVS_IPC_SERVER_AUTH_MANAGER_H
#define VIRT_OVS_IPC_SERVER_AUTH_MANAGER_H

#include <string>

#include "config_module.h"

namespace virt::ovs::ipc::server {

class AuthManager {
public:
    static bool AuthorizeUser(const std::string &username, std::string &authority, config::ConfigModule &conf);
    static bool AuthorizeService(const std::string &authority, const std::string &serviceKey);
};

} // namespace virt::ovs::ipc::server

#endif