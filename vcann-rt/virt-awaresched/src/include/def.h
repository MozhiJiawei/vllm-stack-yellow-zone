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

#ifndef DEF_H
#define DEF_H

#include <cstdint>
#include <string>

namespace vas::common {
// const of space size
constexpr uint16_t BYTE = 8;
constexpr uint64_t MB_SHIFT = 20;
constexpr uint64_t SPACE_1M = static_cast<uint64_t>(1) << MB_SHIFT;
// const of time
constexpr uint16_t MSECS_PER_SEC = 1000;
constexpr uint16_t CLI_TIMEOUT_SECONDS = 30;
// default socket path
const std::string DEFAULT_SOCKET_PATH = "/var/run/vas/vas_uds.sock";
// const of digit
constexpr int16_t NO_1 = 1;
constexpr int16_t NO_2 = 2;
constexpr int16_t NO_3 = 3;
constexpr int16_t NO_4 = 4;
constexpr int16_t NO_32 = 32;
} // namespace vas::common

#endif // DEF_H
