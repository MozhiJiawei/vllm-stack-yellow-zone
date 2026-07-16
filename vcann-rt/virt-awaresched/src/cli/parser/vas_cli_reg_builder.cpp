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
#include "vas_cli_reg_builder.h"

namespace vas::cli::framework {

/**
 * @brief Initialize VasCliRegBuilder
 *
 * Resets SDK command information to default empty state
 */
VasCliRegBuilder::VasCliRegBuilder()
{
    vasCliSdkCmdInfo.command = "";
    vasCliSdkCmdInfo.type = "";
    vasCliSdkCmdInfo.desc = "";
    vasCliSdkCmdInfo.params = {};
    vasCliSdkCmdInfo.vasCliSdkCmdFun = nullptr;
}

/**
 * @brief Set command name
 *
 * Sets the command name for SDK command information
 *
 * @param command Command name to set
 * @return Reference to the current VasCliRegBuilder instance
 */
VasCliRegBuilder &VasCliRegBuilder::SetCommand(const std::string &command)
{
    vasCliSdkCmdInfo.command = command;
    return *this;
}

/**
 * @brief Set command type
 *
 * Sets the type for SDK command information
 *
 * @param type Command type to set
 * @return Reference to the current VasCliRegBuilder instance
 */
VasCliRegBuilder &VasCliRegBuilder::SetType(const std::string &type)
{
    vasCliSdkCmdInfo.type = type;
    return *this;
}

/**
 * @brief Set command description
 *
 * Sets the description for SDK command information
 *
 * @param desc Description text to set
 * @return Reference to the current VasCliRegBuilder instance
 */
VasCliRegBuilder &VasCliRegBuilder::SetDesc(const std::string &desc)
{
    vasCliSdkCmdInfo.desc = desc;
    return *this;
}

/**
 * @brief Add command parameter/option
 *
 * Adds a new parameter with short and long options to SDK command information
 *
 * @param shortOpts Short option name
 * @param longOpts Long option name
 * @param desc Option description
 * @return Reference to the current VasCliRegBuilder instance
 */
VasCliRegBuilder &VasCliRegBuilder::AddParam(const char *shortOpts, const char *longOpts, const char *desc)
{
    VasCliSdkOptionsInfo optsInfo;
    optsInfo.shortOpts = std::string(shortOpts);
    optsInfo.longOpts = std::string(longOpts);
    optsInfo.desc = desc;

    vasCliSdkCmdInfo.params.push_back(optsInfo);
    return *this;
}

/**
 * @brief Set command execution function
 *
 * Assigns the function to be executed when the SDK command is processed
 *
 * @param vasCliSdkCmdFun Function pointer for command execution
 * @return Reference to the current VasCliRegBuilder instance
 */
VasCliRegBuilder &VasCliRegBuilder::SetVasCliSdkCmdFun(VasCliSdkCmdFun vasCliSdkCmdFun)
{
    vasCliSdkCmdInfo.vasCliSdkCmdFun = vasCliSdkCmdFun;
    return *this;
}

/**
 * @brief Build and return SDK command information
 *
 * Returns the fully configured SDK command information
 *
 * @return Configured VasCliSdkCmdInfo object
 */
VasCliSdkCmdInfo VasCliRegBuilder::Build()
{
    return vasCliSdkCmdInfo;
}
} // namespace vas::cli::framework