/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef VAS_SECURITY_MANAGER_H
#define VAS_SECURITY_MANAGER_H

#include <vector>

#include <linux/capability.h>

#include "error.h"

namespace vas::security {
using namespace vas::common;

enum class VasCapOperateType
{
    CAP_ADD,    // Add capabilities
    CAP_DELETE, // Delete capabilities
};

class VasSecurityManager {
public:
    static VasRet GetCapabilities();
    static VasRet SetInitialCapabilities();
    static VasRet ModifyEffectiveCapabilities(const std::vector<__u32> &caps, VasCapOperateType opType);
    static void ClearCapabilities(const std::vector<__u32> &caps);

    static int GetCap(__user_cap_header_struct *capHeader, __user_cap_data_struct *capData);
    static int SetCap(__user_cap_header_struct *capHeader, __user_cap_data_struct *capData);
}; // namespace vas::security

} // namespace vas::security
#endif // VAS_SECURITY_MANAGER_H
