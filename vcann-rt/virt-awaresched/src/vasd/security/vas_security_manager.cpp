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

#include "vas_security_manager.h"

#include <unistd.h>
#include <bitset>
#include <vector>

#include <securec.h>
#include <sys/syscall.h>

#include <linux/capability.h>

#include "logger.h"

namespace vas::security {
using namespace vas::common;

constexpr unsigned long long CAP_SEGMENT_BIT = 32;  // Number of bits processed by a single cap_data structure
constexpr unsigned long long CAP_SEGMENT_COUNT = 2; // Number of cap_data structures

VasRet VasSecurityManager::GetCapabilities()
{
    // Initialize cap_header_t
    __user_cap_header_struct capHeader{};
    capHeader.version = _LINUX_CAPABILITY_VERSION_3; // Use version 3
    capHeader.pid = 0;                               // Get capabilities of the current process

    // Initialize cap_user_data_t
    // Allocate 2 cap_data structures, each handling 32 bits capabilities.
    const auto data = new (std::nothrow) __user_cap_data_struct[CAP_SEGMENT_COUNT];
    if (data == nullptr) {
        return VAS_ERROR_NULLPTR;
    }

    auto ret = memset_s(data, sizeof(__user_cap_data_struct) * CAP_SEGMENT_COUNT, 0,
                        sizeof(__user_cap_data_struct) * CAP_SEGMENT_COUNT);
    if (ret) {
        LOG_ERROR("Failed to clear the data structure.");
        delete[] data;
        return VAS_ERROR;
    }

    // Call syscall(SYS_capget) to get capabilities.
    errno = 0;
    if (GetCap(&capHeader, data) < 0) {
        LOG_ERROR("Failed to get capabilities, ErrorCode=" + std::to_string(errno));
        delete[] data;
        return VAS_ERROR;
    }

    delete[] data;
    return VAS_OK;
}

void SetCapabilitiesData(__user_cap_data_struct *capData)
{
    // Permitted set: Capabilities that a process may possess.
    const std::vector<__u32> pCapabilities = {
        CAP_FOWNER,
        CAP_DAC_OVERRIDE,
    };

    // Effective Set: Currently active capabilities
    const std::vector<__u32> eCapabilities = {};

    // Inheritable Set: Capabilities Inheritable by Child Processes
    const std::vector<__u32> iCapabilities = {};

    // set capabilities
    for (const auto cap : pCapabilities) {
        if (cap >= CAP_SEGMENT_BIT) {
            capData[1].permitted |= (1ULL << (cap - CAP_SEGMENT_BIT));
        } else {
            capData[0].permitted |= (1ULL << cap);
        }
    }

    for (const auto cap : eCapabilities) {
        if (cap >= CAP_SEGMENT_BIT) {
            capData[1].effective |= (1ULL << (cap - CAP_SEGMENT_BIT));
        } else {
            capData[0].effective |= (1ULL << cap);
        }
    }

    for (const auto cap : iCapabilities) {
        if (cap >= CAP_SEGMENT_BIT) {
            capData[1].inheritable |= (1ULL << (cap - CAP_SEGMENT_BIT));
        } else {
            capData[0].inheritable |= (1ULL << cap);
        }
    }
}

VasRet VasSecurityManager::SetInitialCapabilities()
{
    // Initialize cap_header_t and cap_user_data_t
    __user_cap_header_struct capHeader{};
    capHeader.version = _LINUX_CAPABILITY_VERSION_3;
    capHeader.pid = 0;

    // Initialize cap_user_data_t
    const auto capData = new (std::nothrow) __user_cap_data_struct[CAP_SEGMENT_COUNT];
    if (capData == nullptr) {
        return VAS_ERROR_NULLPTR;
    }

    auto ret = memset_s(capData, sizeof(__user_cap_data_struct) * CAP_SEGMENT_COUNT, 0,
                        sizeof(__user_cap_data_struct) * CAP_SEGMENT_COUNT);
    if (ret) {
        LOG_ERROR("Failed to clear the capData structure.");
        delete[] capData;
        return VAS_ERROR;
    }

    SetCapabilitiesData(capData);

    // Call syscall(SYS_capset) to set capabilities.
    errno = 0;
    if (SetCap(&capHeader, capData) < 0) {
        LOG_ERROR("Failed to set capabilities, ErrorCode=" + std::to_string(errno));
        delete[] capData;
        return VAS_ERROR;
    }
    delete[] capData;
    return VAS_OK;
}

VasRet VasSecurityManager::ModifyEffectiveCapabilities(const std::vector<__u32> &caps, VasCapOperateType opType)
{
    __user_cap_header_struct capHeader{};
    capHeader.version = _LINUX_CAPABILITY_VERSION_3;
    capHeader.pid = 0;

    __user_cap_data_struct capData[CAP_SEGMENT_COUNT];

    // 1. Get current capabilities
    errno = 0;
    if (GetCap(&capHeader, capData) < 0) {
        LOG_ERROR("Failed to get capabilities, ErrorCode=" + std::to_string(errno));
        return VAS_ERROR;
    }

    // 2. Security Check: Only operate capabilities that exist in the Permitted set.
    for (const auto cap : caps) {
        if (!(capData[CAP_TO_INDEX(cap)].permitted & CAP_TO_MASK(cap))) {
            LOG_ERROR("Capability not in permitted set, ErrorCode=" + std::to_string(VAS_ERROR_INVAL));
            return VAS_ERROR_INVAL;
        }
    }

    // 3. Modify the Effective set according to the operation type
    switch (opType) {
        case VasCapOperateType::CAP_ADD:
            for (const auto cap : caps) {
                capData[CAP_TO_INDEX(cap)].effective |= CAP_TO_MASK(cap);
            }
            break;
        case VasCapOperateType::CAP_DELETE:
            for (const auto cap : caps) {
                capData[CAP_TO_INDEX(cap)].effective &= ~CAP_TO_MASK(cap);
            }
            break;
        default:
            LOG_ERROR("Invalid operation, ErrorCode=" + std::to_string(VAS_ERROR_INVAL));
            return VAS_ERROR_INVAL;
    }

    if (SetCap(&capHeader, capData) < 0) {
        LOG_ERROR("Failed to set capabilities, ErrorCode=" + std::to_string(errno));
        return VAS_ERROR;
    }

    return VAS_OK;
}

void VasSecurityManager::ClearCapabilities(const std::vector<__u32> &caps)
{
    auto ret = VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_DELETE);
    if (ret != VAS_OK) {
        LOG_ERROR("Delete capabilities failed.");
    }
}

int VasSecurityManager::GetCap(__user_cap_header_struct *capHeader, __user_cap_data_struct *capData)
{
    return static_cast<int>(syscall(SYS_capget, capHeader, capData));
}

int VasSecurityManager::SetCap(__user_cap_header_struct *capHeader, __user_cap_data_struct *capData)
{
    return static_cast<int>(syscall(SYS_capset, capHeader, capData));
}
} // namespace vas::security