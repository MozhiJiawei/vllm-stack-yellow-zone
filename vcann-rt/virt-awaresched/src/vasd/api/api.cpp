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

#include "api.h"

#include "cluster_sched.h"
#include "cluster_vm.h"
#include "cmd.h"
#include "cmd_serialize.h"
#include "logger.h"
#include "vasd_arg_parse.h"

namespace vas::sched {
std::unordered_map<std::string, CmdFunc> Api::cmdMap = {
    {std::string(SET_CMD) + SET_CONFIG_TYPE, SetConfig},
    {std::string(QUERY_CMD) + QUERY_AFFINITY_TYPE, QueryCpuAffinityInfo},
    {std::string(OPTION_CMD) + REASSIGN_TYPE, ReAssign},
};

VasRet Api::SocketMsgHandler(const std::string &cmd, std::string &resStr)
{
    CmdOption cmdOpt;
    LOG_INFO("UDS message comming.");
    if (CmdSerialize::DeSerialize(cmd, cmdOpt) != VAS_OK) {
        resStr = "Failed to deserialize command.";
        return VAS_ERROR;
    }

    if (cmdMap.find(cmdOpt.option) == cmdMap.end()) {
        resStr = "Command option not found. cmd=" + cmdOpt.option;
        return VAS_ERROR;
    }
    LOG_INFO("Command option=" + cmdOpt.option);
    return cmdMap[cmdOpt.option](cmdOpt.params, resStr);
}

VasRet Api::SetConfig(std::map<std::string, std::string> &data, std::string &resStr)
{
    if (data.empty()) {
        resStr = "SetConfig failed, params is empty.";
        LOG_ERROR(resStr);
        return VAS_ERROR;
    }
    if (data.find(SCHED_POLICY) != data.end()) {
        if (data[SCHED_POLICY] != SCHED_POLICY_DYNAMIC && data[SCHED_POLICY] != SCHED_POLICY_STATIC) {
            resStr = "Invalid params, please try with dynamicAffinity or affinity";
            LOG_ERROR(resStr);
            return VAS_ERROR;
        }
        if (VasdArgParse::schedPolicy == data[SCHED_POLICY]) {
            resStr = "SetConfig success.";
            LOG_INFO(resStr);
            return VAS_OK;
        }
        VasRet ret = VasdArgParse::DeInit();
        if (isVasRetFail(ret)) {
            resStr = "SetConfig failed, Failed to clear the existing configuration.";
            LOG_ERROR(resStr);
        }
        std::string originalPolicy = VasdArgParse::schedPolicy;
        VasdArgParse::schedPolicy = data[SCHED_POLICY];
        ret = VasdArgParse::Init();
        if (isVasRetFail(ret)) {
            resStr = "SetConfig failed, Failed to set the new configuration. Rollback to original configuration";
            VasdArgParse::schedPolicy = originalPolicy;
            LOG_ERROR(resStr);
            return VAS_ERROR;
        }
        ClusterSched::GetInstance().ReSetSchedPolicy();
        resStr = "SetConfig success.";
        LOG_INFO(resStr);
        return VAS_OK;
    }
    resStr = "SetConfig failed, Unknown params.";
    LOG_ERROR(resStr);
    return VAS_ERROR;
}

VasRet Api::QueryCpuAffinityInfo(std::map<std::string, std::string> &data, std::string &resStr)
{
    if (data.empty()) {
        resStr = "QueryCpuAffinityInfo failed, params is empty.";
        LOG_ERROR(resStr);
        return VAS_ERROR;
    }
    if (data.find(AFFINITY_SCOPE) != data.end()) {
        std::unordered_map<std::string, VmAffinity> ret;
        ClusterSched::GetInstance().GetAffinityInfo(data[AFFINITY_SCOPE], ret);
        if (ret.empty()) {
            resStr = "No affinity information available";
            LOG_ERROR(resStr);
            return VAS_ERROR;
        }
        std::ostringstream oss;
        oss << R"({)";
        for (auto it = ret.begin(); it != ret.end(); ++it) {
            oss << R"(")" << it->first << R"(":)" << it->second.ToStr();
            if (std::next(it) != ret.end()) {
                oss << R"(,)";
            }
        }
        oss << R"(})";
        resStr = oss.str();
        LOG_INFO("Query cpu affinity info success.");
        return VAS_OK;
    }
    resStr = "QueryCpuAffinityInfo failed, unknown params.";
    LOG_ERROR(resStr);
    return VAS_ERROR;
}

VasRet Api::ReAssign(std::map<std::string, std::string> &data, std::string &resStr)
{
    if (data.empty()) {
        resStr = "ReAssign failed, params is empty.";
        LOG_ERROR(resStr);
        return VAS_ERROR;
    }
    if (data.find(REASSIGN_SCOPE) != data.end()) {
        const auto ret = ClusterSched::GetInstance().ReSchedVm(data[REASSIGN_SCOPE]);
        if (ret == VAS_ERROR_INVAL) {
            resStr = "Virtual machine needs to reassign cpu does not exist";
            LOG_ERROR(resStr);
            return VAS_ERROR;
        } else if (ret != VAS_OK) {
            resStr = "ReAssign failed.";
            LOG_ERROR(resStr);
            return VAS_ERROR;
        }
        resStr = "ReAssign success.";
        LOG_INFO(resStr);
        return ret;
    }
    resStr = "ReAssign failed, unknown params.";
    return VAS_ERROR;
}

} // namespace vas::sched