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
#include "client_library.h"

#include <dlfcn.h>
#include <utility>

namespace virt::ovs::ubse::client {
ClientLibrary &ClientLibrary::Instance(const std::string &soPath)
{
    static ClientLibrary instance(soPath);
    return instance;
}

ClientLibrary::ClientLibrary(std::string soPath) : path(std::move(soPath)){};

ClientLibrary::~ClientLibrary()
{
    if (handle) {
        dlclose(handle);
        handle = nullptr;
    }
};

void ClientLibrary::Open()
{
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        throw std::runtime_error(dlerror());
    }
}

void *ClientLibrary::GetSymbol(const std::string &symbol)
{
    std::call_once(loadOnce, &ClientLibrary::Open, this);
    if (!handle) {
        throw std::runtime_error("shared library not opened");
    }

    void *sym = dlsym(handle, symbol.c_str());
    if (!sym) {
        throw std::runtime_error("failed to get symbol from shared library, symbol is" + symbol);
    }
    return sym;
}
} // namespace virt::ovs::ubse::client
