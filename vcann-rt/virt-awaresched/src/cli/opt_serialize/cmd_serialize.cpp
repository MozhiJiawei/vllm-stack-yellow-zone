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
#include "cmd_serialize.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cmd.h"
#include "error.h"

namespace vas::common {

constexpr int16_t KEY_VALUE_SIZE = 2;

VasRet CmdSerialize::Serialize(const CmdOption &cmdOpt, std::string &cmdStr)
{
    std::ostringstream oss;
    oss << cmdOpt.option;
    for (const auto &[fst, snd] : cmdOpt.params) {
        oss << ";" << fst << ":" << snd;
    }
    cmdStr = oss.str();
    return VAS_OK;
}

VasRet CmdSerialize::DeSerialize(const std::string &cmdStr, CmdOption &cmdOpt)
{
    std::istringstream iss(cmdStr);
    std::string tmpStr;
    std::vector<std::string> tokens;

    while (std::getline(iss, tmpStr, ';')) {
        tokens.push_back(tmpStr);
    }
    if (tokens.empty()) {
        return VAS_ERROR;
    }

    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        if (it == tokens.begin()) {
            cmdOpt.option = *it;
            continue;
        }
        std::istringstream paramIss(*it);
        std::vector<std::string> keyValue;
        while (std::getline(paramIss, tmpStr, ':')) {
            keyValue.push_back(tmpStr);
        }
        if (keyValue.size() == KEY_VALUE_SIZE) {
            cmdOpt.params[keyValue[0]] = keyValue[1];
        }
    }
    return VAS_OK;
}
} // namespace vas::common