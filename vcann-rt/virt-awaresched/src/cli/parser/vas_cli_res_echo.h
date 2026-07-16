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

#ifndef VAS_CLI_SDK_ECHO_H
#define VAS_CLI_SDK_ECHO_H

#include <string>

#include "vas_cli_parse.h"

namespace vas::cli::framework {
class VasCliResEcho {
public:
    static VasCliResEcho &GetInstance()
    {
        static VasCliResEcho instance;
        return instance;
    }
    VasRet ExecuteCommand();

private:
    VasCliResEcho() {}
    VasCliResEcho(const VasCliResEcho &) = delete;
    VasCliResEcho &operator=(const VasCliResEcho &) = delete;
    VasCliSdkCmdInfo cliSdkCmdInfo{};
    VasRet StringPrintDisplay(const std::string &content);
    VasRet IndividualWordsDisplay(const std::string &content);
};
} // namespace vas::cli::framework
#endif // VAS_CLI_SDK_ECHO_H