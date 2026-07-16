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
#ifndef VAS_CLI_PARSE_H
#define VAS_CLI_PARSE_H

#include <string>
#include <vector>

#include "error.h"
#include "vas_cli_reg_builder.h"

namespace vas::cli::framework {
using namespace vas::common;

constexpr int MAX_CMD_NUM = 20;
constexpr int MAX_CMD_OR_TYPE_LENGTH = 64;
constexpr int MAX_OPTIONS_LENGTH = 64;
constexpr int MAX_OPTIONS_NUM = 10;
constexpr int MAX_VALUE_LENGTH = 1024;

class VasCliParse {
public:
    static VasCliParse &GetInstance()
    {
        static VasCliParse instance;
        return instance;
    }

    static void VasCliRegisterSdkCmdInfo(std::vector<VasCliSdkCmdInfo> &vasCliSdkCmdInfo);
    VasRet SdkCliParse(const std::vector<std::string> &args);
    VasCliSdkCmdInfo GetSdkCommandInfo();
    const std::map<std::string, std::string> &GetInputOptionMap() const;
    static void PrintWithWordWrap(const std::string &text);
    void PrtHelp(const std::vector<std::string> &args);
    bool needPrtHelp = false;
    const std::vector<VasCliSdkCmdInfo> &GetSdkCmdInfo() const;
    const std::unordered_map<std::string, std::vector<VasCliSdkOptionsInfo>> &GetSdkCommandWithOptions() const;
    void Reset();

private:
    std::map<std::string, std::string> inputOptionMap{}; // Input parameter mapping
    VasCliSdkCmdInfo sdkCommandInfo;                     // Matched SDK command information
    std::vector<VasCliSdkCmdInfo> sdkCmdInfo;            // Command information registered for SDK invocation
    std::unordered_set<std::string> fullCommand;         // Complete command keys
    std::unordered_map<std::string, std::vector<VasCliSdkOptionsInfo>> sdkCommandWithOptions; // Commands parameters

    static bool CheckDuplicateOptions(const std::unordered_set<std::string> &shortOptions,
                                      const std::unordered_set<std::string> &longOptions, const std::string &shortOpt,
                                      const std::string &longOpt);
    static bool CheckOptionsNum(const std::vector<VasCliSdkOptionsInfo> &params);
    static bool CheckCommandTypeLength(const std::string &command, const std::string &type);
    static bool CheckOptionsLength(const VasCliSdkOptionsInfo &opts);
    void SetSdkCmdInfoWithOptsMap(const VasCliSdkCmdInfo &cmdInfo);
    void SdkCmdInfoRegister(std::vector<VasCliSdkCmdInfo> &regInfo);

    // Pixel Retreat Length for Annotation Line Breaks: The total pixel width to be retracted when wrapping descriptive
    // text, ensuring precise alignment and readability across different terminal configurations.
    const int indentSize = 46;
    void ParsePrtHelpInfo();
    void ParseOneCommandPrtHelpInfo(const std::string &firstCommand, const std::string &secondCommand);
    void CommandTypeParamsHelpInfo(const std::vector<VasCliSdkOptionsInfo> &params, int lineLimit);

    // Formatted output
    static size_t GetTerminalWidth();
    static std::string HandleTab(const std::string &text, size_t currentLength);
    static void AddWordToLine(std::string &currentLine, const std::string &word, size_t lineLimit);
    void PrintWithLineLimit(const std::string &str, int lineLimit, const std::string &delimiter) const;

    // Parse command line arguments
    VasRet GenSdkCmdOptParaMap(const std::vector<std::string> &args, const std::string &param,
                               const std::vector<VasCliSdkOptionsInfo> &cmdOptions, size_t &paramIndex,
                               std::map<std::string, std::string> &params);
    VasRet GetSdkRegCmdInfo(const std::vector<std::string> &args);
    VasRet SdkCmdOptParse(const std::vector<std::string> &args, std::map<std::string, std::string> &inputOptionMap);
    static bool CheckOptionValueLength(const std::string &optionValue);
};
} // namespace vas::cli::framework
#endif // VAS_CLI_PARSE_H
