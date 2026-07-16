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

#ifndef UBS_VIRT_MACROS_H
#define UBS_VIRT_MACROS_H

#include "service_base.h"
namespace virt::ovs {

// REGISTER_SERVICE macro:
// Automatically registers a service class into ServiceRegistry
// during program startup (before main() is executed)
//
// Usage:
//    REGISTER_SERVICE(MyService)
#ifndef DISABLE_SERVICE_AUTO_REGISTER
#define REGISTER_SERVICE(SERVICE_CLASS)                                                     \
    namespace {                                                                             \
    struct SERVICE_CLASS##Registrar {                                                       \
        SERVICE_CLASS##Registrar()                                                          \
        {                                                                                   \
            ServiceRegistry::Instance().RegisterService(std::make_shared<SERVICE_CLASS>()); \
        }                                                                                   \
    };                                                                                      \
    static SERVICE_CLASS##Registrar global_##SERVICE_CLASS##_Registrar;                     \
    } // namespace
#else
#define REGISTER_SERVICE(...) /* auto registration disabled for UT */
#endif
} // namespace virt::ovs
#endif // UBS_VIRT_MACROS_H
