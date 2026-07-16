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

#ifndef VIRT_OVS_COMMON_CONSTANTS_H
#define VIRT_OVS_COMMON_CONSTANTS_H

#include <cstddef>
#include <cstdint>

namespace virt::ovs::constants {

inline constexpr int MAX_EPOLL_EVENTS = 64;
inline constexpr int EPOLL_WAIT_TIMEOUT_MS = 1000;
inline constexpr int LISTEN_BACKLOG = 128;
inline constexpr int MAX_BUFFER_SIZE = 1024;
inline constexpr int MAX_BODY_BUFFER_SIZE = 4096;
inline constexpr int DEFAULT_QPS_LIMIT = 100;

inline constexpr int THREAD_POOL_DEFAULT_QUEUE_SIZE = 100;

inline constexpr uint32_t GB_TO_MB = 1024;

inline constexpr size_t CONFIG_DIR_MAX_DEPTH = 3;
inline constexpr size_t CONFIG_MAX_LINES = 1000;
inline constexpr size_t SUFFIX_SIZE = 5;
inline constexpr size_t CONFIG_MIN_FIELD_LENGTH = 1;
inline constexpr size_t CONFIG_SECTION_MAX_FIELD_LENGTH = 64;
inline constexpr size_t CONFIG_KEY_MAX_FIELD_LENGTH = 64;
inline constexpr size_t CONFIG_VALUE_MAX_FIELD_LENGTH = 256;

} // namespace virt::ovs::constants

#endif