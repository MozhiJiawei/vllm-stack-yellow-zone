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
#include "vasctl_arg_parse.h"

#include <iostream>

#include "cluster_sched.h"
#include "cmd_serialize.h"
#include "cpu_helper.h"
#include "error.h"
#include "libvirt_helper.h"
#include "logger.h"
#include "socket_client.h"
#include "vas_cli_parse.h"
#include "vas_cli_reg_builder.h"

using namespace vas::cli::framework;
using namespace vas::common;
using namespace vas::sched::acquire;
using namespace vas::sched;

namespace vas::cli::reg {

/**
 * @brief Command registration format
 * @details
 * - command: Command name
 * - type: Command type
 * - description: Command description
 * - param opt: Parameter option
 * - param long opt: Long parameter option
 * - param description: Parameter description
 * - invalid param message: Invalid parameter message
 */
constexpr auto INVALID_PARAM = "ERROR: Missing the specific param."
                               " Please try 'vasctl --help' for more info.";
constexpr auto SUCCESS_EXECUTE = "Success to execute command";
constexpr auto FAILED_EXECUTE = "Failed to execute command";
constexpr auto FAILED_SERIALIZE = "Failed to serialize command";

inline VasCliSdkResult PromptReply(const std::string &str)
{
    return {VasCliSdkResultType::PROMPT, str, {}};
}

VasCliSdkResult CliSetConfFunc(const std::map<std::string, std::string> &params)
{
    // Parameter validation
    if (params.find(SCHED_POLICY) == params.end()) {
        return PromptReply(INVALID_PARAM);
    }

    // Serialization
    const CmdOption cmdOpt{
        .option = std::string(SET_CMD) + SET_CONFIG_TYPE,
        .params = params,
    };
    std::string cmdStr;
    if (const VasRet ret = CmdSerialize::Serialize(cmdOpt, cmdStr); ret != VAS_OK) {
        return PromptReply(FAILED_SERIALIZE);
    }

    // Send message
    if (!SendSingleMessage(cmdStr)) {
        return PromptReply(FAILED_EXECUTE);
    }
    return PromptReply(SUCCESS_EXECUTE);
}

VasCliSdkResult CliQueryAffinityFunc(const std::map<std::string, std::string> &params)
{
    // Parameter validation
    if (params.find(AFFINITY_SCOPE) == params.end()) {
        return PromptReply(INVALID_PARAM);
    }

    // Serialization
    std::string cmdStr;
    const CmdOption cmdOpt{
        .option = std::string(QUERY_CMD) + QUERY_AFFINITY_TYPE,
        .params = params,
    };
    if (const VasRet ret = CmdSerialize::Serialize(cmdOpt, cmdStr); ret != VAS_OK) {
        return PromptReply(FAILED_SERIALIZE);
    }

    // Send message
    if (!SendSingleMessage(cmdStr)) {
        return PromptReply(FAILED_EXECUTE);
    }
    return PromptReply(SUCCESS_EXECUTE);
}

VasCliSdkResult CliOptReassignFunc(const std::map<std::string, std::string> &params)
{
    // Parameter validation
    if (params.find(REASSIGN_SCOPE) == params.end()) {
        return PromptReply(INVALID_PARAM);
    }

    // Serialization
    std::string cmdStr;
    const CmdOption cmdOpt{
        .option = std::string(OPTION_CMD) + REASSIGN_TYPE,
        .params = params,
    };
    if (const VasRet ret = CmdSerialize::Serialize(cmdOpt, cmdStr); ret != VAS_OK) {
        return PromptReply(FAILED_SERIALIZE);
    }

    // Send message
    if (!SendSingleMessage(cmdStr)) {
        return PromptReply(FAILED_EXECUTE);
    }
    return PromptReply(SUCCESS_EXECUTE);
}

VasCliSdkResult CliOptRecoverFunc(const std::map<std::string, std::string> &params)
{
    // Parameter validation
    auto ret = Logger::Instance().Init(LOGPATH, LOGFILE, MAX_LOGFILESIZE, MAX_LOGFILE, OutputType::NONE);
    if (isVasRetFail(ret)) {
        std::cout << "Log module init failed." << std::endl;
        return PromptReply(FAILED_EXECUTE);
    }
    ret = CpuHelper::Init();
    if (isVasRetFail(ret)) {
        std::cout << "Cpu helper init failed." << std::endl;
        return PromptReply(FAILED_EXECUTE);
    }
    if (const auto cpuTopologyMap = CpuHelper::GetInstance().GenCpuTopology(); cpuTopologyMap.empty()) {
        std::cout << "Get Cpu topology info failed." << std::endl;
        return PromptReply(FAILED_EXECUTE);
    }
    ret = LibvirtHelper::GetInstance().Init();
    if (isVasRetFail(ret)) {
        std::cout << "Libvirt helper init failed." << std::endl;
        LibvirtHelper::GetInstance().DeInit();
        return PromptReply(FAILED_EXECUTE);
    }
    VmInfoMap vmInfoMap;
    ret = LibvirtHelper::GetInstance().GetVmInfoList(vmInfoMap);
    if (isVasRetFail(ret)) {
        std::cout << "Get vm info failed." << std::endl;
        LibvirtHelper::GetInstance().DeInit();
        return PromptReply(FAILED_EXECUTE);
    }
    ret = Logger::Instance().Init(LOGPATH, LOGFILE, MAX_LOGFILESIZE, MAX_LOGFILE, OutputType::STDOUT);
    if (isVasRetFail(ret)) {
        std::cout << "Log module init failed. outType=" << static_cast<uint16_t>(OutputType::STDOUT) << std::endl;
        LibvirtHelper::GetInstance().DeInit();
        return PromptReply(FAILED_EXECUTE);
    }
    ClusterSched::GetInstance().RecoverVmVcpu(vmInfoMap);
    return PromptReply(SUCCESS_EXECUTE);
}

VasCliSdkCmdInfo SetConfig()
{
    SdkCmdInfoBuilder builder;
    builder.SetCommand(SET_CMD)
        .SetType(SET_CONFIG_TYPE)
        .SetDesc(SET_CONFIG_DES)
        .AddParam(SCHED_POLICY_SHORT, SCHED_POLICY, SCHED_POLICY_DES)
        .SetVasCliSdkCmdFun(CliSetConfFunc);
    return std::move(builder.Build());
}

VasCliSdkCmdInfo QueryAffinity()
{
    SdkCmdInfoBuilder builder;
    builder.SetCommand(QUERY_CMD)
        .SetType(QUERY_AFFINITY_TYPE)
        .SetDesc(QUERY_AFFINITY_DES)
        .AddParam(AFFINITY_SCOPE_SHORT, AFFINITY_SCOPE, AFFINITY_SCOPE_DES)
        .SetVasCliSdkCmdFun(CliQueryAffinityFunc);
    return std::move(builder.Build());
}

VasCliSdkCmdInfo Reassign()
{
    SdkCmdInfoBuilder builder;
    builder.SetCommand(OPTION_CMD)
        .SetType(REASSIGN_TYPE)
        .SetDesc(REASSIGN_DES)
        .AddParam(REASSIGN_SCOPE_SHORT, REASSIGN_SCOPE, REASSIGN_SCOPE_DES)
        .SetVasCliSdkCmdFun(CliOptReassignFunc);
    return std::move(builder.Build());
}

VasCliSdkCmdInfo Recover()
{
    SdkCmdInfoBuilder builder;
    builder.SetCommand(OPTION_CMD).SetType(RECOVER_TYPE).SetDesc(RECOVER_DES).SetVasCliSdkCmdFun(CliOptRecoverFunc);
    return std::move(builder.Build());
}

void RegisterCliModuleSDK()
{
    std::vector<VasCliSdkCmdInfo> demo;
    demo.emplace_back(std::move(SetConfig()));
    demo.emplace_back(std::move(QueryAffinity()));
    demo.emplace_back(std::move(Reassign()));
    demo.emplace_back(std::move(Recover()));
    VasCliParse::VasCliRegisterSdkCmdInfo(demo);
}
} // namespace vas::cli::reg