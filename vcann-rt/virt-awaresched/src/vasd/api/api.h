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

#ifndef API_H
#define API_H

#include <map>
#include <unordered_map>

#include "cmd.h"
#include "error.h"

namespace vas::sched {
using namespace vas::common;
using CmdFunc = VasRet (*)(std::map<std::string, std::string> &, std::string &);

class Api {
public:
    /**
     * @brief Handle socket message command
     *
     * Parses and processes an incoming command string by deserializing it,
     * validating the command option, and executing the corresponding handler.
     * @param cmd [IN] The command string to be deserialized and processed
     * @param resStr [OUT] Reference to a string that will store the response or error message
     * @return VAS_OK on success, VAS_ERROR on failure
     */
    static VasRet SocketMsgHandler(const std::string &cmd, std::string &resStr);

private:
    static std::unordered_map<std::string, CmdFunc> cmdMap;

    static VasRet SetConfig(std::map<std::string, std::string> &data, std::string &resStr);
    static VasRet QueryCpuAffinityInfo(std::map<std::string, std::string> &data, std::string &resStr);
    static VasRet ReAssign(std::map<std::string, std::string> &data, std::string &resStr);
};

} // namespace vas::sched

#endif // API_H
