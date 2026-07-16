/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef EBPF_LOGGER_MACROS_H
#define EBPF_LOGGER_MACROS_H

#include <sstream>

#include "ebpf_logger.h"

#define LOG_INTERNAL(level, message)                                                          \
    do {                                                                                      \
        std::ostringstream oss;                                                               \
        oss << (message);                                                                     \
        EbpfLogger::getInstance().logMessage(level, oss.str(), __FILE__, __func__, __LINE__); \
    } while (0)

#define EBPF_LOG_DEBUG(msg) LOG_INTERNAL(EbpfLogger::LogLevel::DEBUG, msg)
#define EBPF_LOG_INFO(msg) LOG_INTERNAL(EbpfLogger::LogLevel::INFO, msg)
#define EBPF_LOG_WARN(msg) LOG_INTERNAL(EbpfLogger::LogLevel::WARNING, msg)
#define EBPF_LOG_ERROR(msg) LOG_INTERNAL(EbpfLogger::LogLevel::ERROR, msg)
#define EBPF_LOG_FATAL(msg) LOG_INTERNAL(EbpfLogger::LogLevel::FATAL, msg)
#endif // EBPF_LOGGER_MACROS_H
