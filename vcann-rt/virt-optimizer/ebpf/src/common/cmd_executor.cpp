/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "cmd_executor.h"

#include <malloc.h>
#include <iostream>
#include <utility>

#include "log/ebpf_logger_macros.h"

constexpr unsigned int BUFFER_SIZE = 255; // Buffer for accepting command output

CmdExecutor::CmdExecutor(std::string hostname) : hostname(std::move(hostname)) {}

std::pair<bool, std::string> CmdExecutor::runCommand(const std::string &cmd)
{
    std::string cmdChar;
    if (hostname.empty()) {
        // Whitelist control
        cmdChar = cmd + " 2>&1";
    } else {
        // Whitelist control
        cmdChar = "ssh " + hostname + " \" " + cmd + " \" 2>&1";
    }
    FILE *pipe = popen(cmdChar.c_str(), "r");
    if (!pipe) {
        EBPF_LOG_ERROR("Failed to run command.");
        return std::make_pair(false, "");
    }
    std::string output;
    static char buf[BUFFER_SIZE];
    // Waiting for the guest to close the pipe.
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        output.append(buf);
    }

    auto statusCode = static_cast<unsigned int>(pclose(pipe));
    malloc_trim(0);
    if (WIFEXITED(statusCode) && (WEXITSTATUS(statusCode) == 0)) {
        return std::make_pair(true, std::move(output));
    }
    EBPF_LOG_ERROR("Command return non-zero value. Status code: " + std::to_string(statusCode));
    return std::make_pair(false, std::move(output));
}
