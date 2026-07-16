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
#include "vas_cli_res_echo.h"

#include <iostream>

#include "def.h"
#include "vas_cli_parse.h"

namespace vas::cli::framework {
using namespace vas::common;

/**
 * @brief Display content with word wrapping
 *
 * Prints the given content using word wrapping functionality
 *
 * @param content Text to be displayed
 * @return VasRet Execution status
 */
VasRet VasCliResEcho::IndividualWordsDisplay(const std::string &content)
{
    VasCliParse::PrintWithWordWrap(content);
    return VAS_OK;
}

/**
 * @brief Display content with word wrapping
 *
 * Prints the given content using word directly with whole string phrase
 *
 * @param content Text to be displayed
 * @return VasRet Execution status
 */
VasRet VasCliResEcho::StringPrintDisplay(const std::string &content)
{
    std::cout << content << std::endl;
    return VAS_OK;
}

/**
 * @brief Execute CLI SDK Command
 *
 * Processes and executes SDK command based on parsed input,
 * handling different result types (prompt or table display)
 *
 * @return VasRet Command execution result status
 */
VasRet VasCliResEcho::ExecuteCommand()
{
    uint32_t ret = 0;
    cliSdkCmdInfo = VasCliParse::GetInstance().GetSdkCommandInfo();
    if (cliSdkCmdInfo.vasCliSdkCmdFun == nullptr) {
        return VAS_ERROR;
    }
    VasCliSdkResult result = cliSdkCmdInfo.vasCliSdkCmdFun(VasCliParse::GetInstance().GetInputOptionMap());
    auto EndFunc = [&ret]() {
        if (ret != VAS_OK) {
            VasCliParse::PrintWithWordWrap("ERROR: Failed to display table. Please try '--help' for more info.\n");
        }
    };
    switch (result.vasCliSdkResultType) {
        case VasCliSdkResultType::NORMAL: {
            ret = StringPrintDisplay(result.vasCliSdkResultByString);
            EndFunc();
            return ret;
        }
        case VasCliSdkResultType::PROMPT: {
            ret = IndividualWordsDisplay(result.vasCliSdkResultByString);
            EndFunc();
            return ret;
        }
        default: {
            EndFunc();
            return VAS_ERROR;
        }
    }
}
} // namespace vas::cli::framework