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
#ifndef VAS_CLI_CMD_BUILDER_H
#define VAS_CLI_CMD_BUILDER_H

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vas::cli::framework {

// Return value type
enum class VasCliSdkResultType
{
    NORMAL,
    PROMPT,
};

// Table structure
struct VariableCellInfo {
    int rows{};
    int cols{};
    int maxWidth{};
    std::vector<int> columnWidths{};
    std::vector<std::vector<std::string>> cellDatas{};
    std::unordered_set<int> lineSeparateIndex{};
    std::unordered_set<int> noSeparateIndex{};
    std::unordered_map<int, std::map<int, std::string>> mergeCellData{};
};

// Result data structure
struct VasCliSdkResult {
    VasCliSdkResultType vasCliSdkResultType;
    std::string vasCliSdkResultByString;
    std::vector<VariableCellInfo> variableCellInfo;
};

using VasCliSdkCmdFun = VasCliSdkResult (*)(const std::map<std::string, std::string> &params);

struct VasCliSdkOptionsInfo {
    std::string shortOpts;
    std::string longOpts;
    std::string desc;
    bool operator==(const VasCliSdkOptionsInfo &other) const
    {
        return shortOpts == other.shortOpts && longOpts == other.longOpts && desc == other.desc;
    }
};

struct VasCliSdkCmdInfo {
    std::string command;
    std::string type;
    std::string desc;
    std::vector<VasCliSdkOptionsInfo> params;
    VasCliSdkCmdFun vasCliSdkCmdFun;
    bool operator==(const VasCliSdkCmdInfo &other) const
    {
        return command == other.command && type == other.type && desc == other.desc && params == other.params &&
               vasCliSdkCmdFun == other.vasCliSdkCmdFun;
    }
};

class VasCliRegBuilder {
public:
    VasCliRegBuilder();

    VasCliRegBuilder &SetCommand(const std::string &command);

    VasCliRegBuilder &SetType(const std::string &type);

    VasCliRegBuilder &SetDesc(const std::string &desc);

    VasCliRegBuilder &AddParam(const char *shortOpts, const char *longOpts, const char *desc);

    VasCliRegBuilder &SetVasCliSdkCmdFun(VasCliSdkCmdFun vasCliSdkCmdFun = nullptr);

    VasCliSdkCmdInfo Build();

private:
    VasCliSdkCmdInfo vasCliSdkCmdInfo;
};
using SdkCmdInfoBuilder = VasCliRegBuilder;
} // namespace vas::cli::framework
#endif // VAS_CLI_CMD_BUILDER_H
