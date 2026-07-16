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
#ifndef UBSVIRTOVS_CONFIG_COMMON_DEF_H
#define UBSVIRTOVS_CONFIG_COMMON_DEF_H

#include <cstdint>
#include "common/constants.h"

namespace virt::ovs::config {
using virt::ovs::constants::CONFIG_DIR_MAX_DEPTH;
using virt::ovs::constants::CONFIG_KEY_MAX_FIELD_LENGTH;
using virt::ovs::constants::CONFIG_MAX_LINES;
using virt::ovs::constants::CONFIG_MIN_FIELD_LENGTH;
using virt::ovs::constants::CONFIG_SECTION_MAX_FIELD_LENGTH;
using virt::ovs::constants::CONFIG_VALUE_MAX_FIELD_LENGTH;
using virt::ovs::constants::SUFFIX_SIZE;

enum class ConfigCode : uint32_t
{
    OK = 0,

    CONFIG_FOLDER_MAX_DEPTH = 1001,
    CONFIG_FOLDER_OPEN_ERROR = 1002,
    CONFIG_FILE_READ_ERROR = 1003,

    SECTION_NOT_EXIST = 2001,
    CONFIG_KEY_NOT_EXIST = 2002,
    SECTION_LENGTH_INVALID = 2003,
    KEY_LENGTH_INVALID = 2004,
    VALUE_LENGTH_INVALID = 2005,
    VALUE_TYPE_NOT_SUPPORTED = 2006,
    CONFIG_VALUE_INVALID = 2007,
    CONFIG_ARGUMENT_INVALID = 2009,
    CONFIG_OUT_OF_RANGE = 2008,

    MEM_ALLOCATE_ERROR = 3001,
};
} // namespace virt::ovs::config
#endif // UBSVIRTOVS_CONFIG_COMMON_DEF_H
