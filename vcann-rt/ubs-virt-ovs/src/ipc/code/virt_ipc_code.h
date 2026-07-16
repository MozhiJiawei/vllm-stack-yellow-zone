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

#ifndef UBS_VIRT_IPC_CODE_H
#define UBS_VIRT_IPC_CODE_H
#include <cstdint>
namespace virt::ovs {
enum class VirtIPCCode : uint32_t
{
    OK = 0,

    SERVICE_NOT_FOUND = 1001,
    METHOD_NOT_FOUND = 1002,
    DESERIALIZE_ERROR = 1003,
    INTERNAL_ERROR = 1004,
    INVALID_PARAM = 1005,
    ALREADY_EXIST = 1006,
    NOT_EXIST = 1007,
    UBSE_ERROR = 1008,
    PERMISSION_DENIED = 1009,
};
} // namespace virt::ovs
#endif // UBS_VIRT_IPC_CODE_H
