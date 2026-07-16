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

#ifndef CLIENT_LIBRARY_H
#define CLIENT_LIBRARY_H

#include <mutex>
#include <string>

#include "logger.h"

namespace virt::ovs::ubse::client {
class ClientLibrary {
public:
    static ClientLibrary &Instance(const std::string &soPath = "/usr/lib64/libubse-client.so");

    void *GetSymbol(const std::string &symbol);

private:
    ~ClientLibrary();

    void Open();

    std::once_flag loadOnce;

    explicit ClientLibrary(std::string soPath);

    std::string path;
    void *handle{nullptr};
};
} // namespace virt::ovs::ubse::client

#endif // CLIENT_LIBRARY_H
