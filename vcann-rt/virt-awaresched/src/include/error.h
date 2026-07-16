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

/**
 * Generate private error codes within the module.
 * (Module ID + Specific error code within the module = Complete error code)
 * MID (high two bytes) + ERRNO (low two bytes)
 * For example, error code: 0x10081000
 */
#ifndef ERROR_H
#define ERROR_H

#include <sstream>
#include <string>

namespace vas::common {
using VasRet = uint32_t;
// Module code bit length
constexpr uint32_t BYTE_NUM16 = 16;
// Each subsystem, MID segment starting ID definition, select the corresponding starting ID when defining each module.
#define VAS_MID_BEGIN0 0x0000 // common
#define VAS_MID_BEGIN1 0x1000 // vasd error
#define VAS_MID_BEGIN2 0x2000 // vasctl error

/**
 * Generate a 32-bit module error base code (the module error code is located in the upper 16 bits).
 * @param moduleErrorBegin Module ID Starting ErrorCode
 * @param n Module ID
 * @return 32-bit module error base code
 */
inline VasRet MIDErrorCode(const VasRet moduleErrorBegin, const VasRet n)
{
    return (moduleErrorBegin + n) << BYTE_NUM16;
}

/**
 * Error code
 */
#define VAS_MID_API MIDErrorCode(VAS_MID_BEGIN1, 1) /* 0X1001 api */

/* ********************************************* */
/* Common error code definitions, globally unique, record the standard error returns of the system. */
/* ********************************************* */

#define VAS_OK (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0000)                        // Correct
#define VAS_ERROR (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0001)                     // Error
#define VAS_ERROR_CMD (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0002)                 // No such file or directory
#define VAS_ERROR_NOMEM (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0003)               // Out of memory
#define VAS_ERROR_ACCES (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0004)               // Permission denied
#define VAS_ERROR_SRCH (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0005)                // No such process
#define VAS_ERROR_EXIST (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0006)               // File exists
#define VAS_ERROR_NOSPC (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0007)               // No space left on device
#define VAS_ERROR_AGAIN (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0008)               // Try again
#define VAS_ERROR_IO (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0009)                  // I/O error
#define VAS_ERROR_BADF (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000A)                // Bad file descriptor
#define VAS_ERROR_CONF_INVALID (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000B)        // Invalid config
#define VAS_ERROR_NULLPTR (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000C)             // Nullptr
#define VAS_MASTER_EMPTY_VECTOR_ERROR (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000D) // Empty Array
#define VAS_ERROR_INVAL (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000E)               // Invalid argument
#define VAS_ERROR_EXCEEDS_RANGE (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x000F)       // Out of range
#define VAS_WARN (MIDErrorCode(VAS_MID_BEGIN0, 0) | 0x0010)                      // Alarm

inline std::string formatRetCode(const VasRet ret)
{
    std::ostringstream oss;
    oss << "retCode=" << std::hex << std::uppercase << ret;
    return oss.str();
}

inline bool isVasRetFail(const VasRet ret)
{
    return ret != VAS_OK;
}

inline bool isVasRetOk(const VasRet ret)
{
    return ret == VAS_OK;
}

inline bool isIntInvalid(const int ret)
{
    return ret == -1;
}

inline bool isIntEqZero(const int ret)
{
    return ret == 0;
}

} // namespace vas::common

#endif // ERROR_H
