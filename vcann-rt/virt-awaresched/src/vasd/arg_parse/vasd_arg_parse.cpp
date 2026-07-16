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
#include "vasd_arg_parse.h"

#include <fstream>
#include <iostream>
#include <vector>

#include "conf.h"
#include "logger.h"
#include "vas_cli_parse.h"
#include "vas_cli_reg_builder.h"
#include "vas_security_manager.h"

namespace vas::sched {
using namespace vas::security;
bool VasdArgParse::smt = true;
std::string VasdArgParse::schedPolicy = "affinity";
uint16_t VasdArgParse::dynamicAffinityUtilThresh = 85;
std::string VasdArgParse::skippedCPUSet = "";
bool VasdArgParse::rangeAffinity = true;

/**
 * Init args after SetVasCliSdkCmdFun
 * @return Initialization result.
 */
VasRet VasdArgParse::Init()
{
    if (schedPolicy == "dynamicAffinity") {
        if (!IsDynamicAffinityAvailable()) {
            schedPolicy = "affinity";
            LOG_WARN("Dynamic affinity is not available. Please add 'dynamic_affinity=enable' to the cmdline.");
            return VAS_WARN;
        }

        if (const auto ret = WriteDynamicAffinityUtilThresh(dynamicAffinityUtilThresh); ret != VAS_OK) {
            LOG_WARN("Failed to set dynamic affinity utilization threshold.");
            return VAS_WARN;
        }
        return VAS_OK;
    }
    if (schedPolicy != "affinity") {
        LOG_ERROR("Scheduler policy is invalid. current=" + schedPolicy);
        return VAS_ERROR;
    }
    return VAS_OK;
}

VasRet VasdArgParse::DeInit()
{
    if (schedPolicy != "affinity" && schedPolicy != "dynamicAffinity") {
        LOG_ERROR("Scheduler policy is invalid. current=" + schedPolicy);
        return VAS_ERROR;
    }
    return VAS_OK;
}

/**
 * Write the dynamic affinity utilization threshold to the specified file.
 * @param value The utilization threshold value to write.
 * @return Operation result.
 */
VasRet VasdArgParse::WriteDynamicAffinityUtilThresh(uint16_t value)
{
    std::vector<__u32> caps = {
        CAP_DAC_OVERRIDE,
    };
    try {
        if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_ADD) != VAS_OK) {
            LOG_ERROR("Add capabilities failed.");
            return VAS_ERROR;
        }
        std::ofstream file(DYNAMIC_UTIL_THRESH_PATH, std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open file: " + DYNAMIC_UTIL_THRESH_PATH.string());
            VasSecurityManager::ClearCapabilities(caps);
            return VAS_ERROR;
        }
        file << value << std::flush;
        if (file.fail()) {
            LOG_ERROR("Failed to write to file. value=" + std::to_string(value));
            VasSecurityManager::ClearCapabilities(caps);
            return VAS_ERROR;
        }
        LOG_INFO("Successfully set sched_util_low_pct. value=" + std::to_string(value));
        if (VasSecurityManager::ModifyEffectiveCapabilities(caps, VasCapOperateType::CAP_DELETE) != VAS_OK) {
            LOG_ERROR("Delete capabilities failed.");
            return VAS_ERROR;
        }
        return VAS_OK;
    } catch (const std::exception &e) {
        LOG_ERROR("Exception during file write: " + std::string(e.what()));
        VasSecurityManager::ClearCapabilities(caps);
        return VAS_ERROR;
    }
}

/**
 * Check if dynamic affinity is available by examining the proc cmdline.
 * @return True if dynamic affinity is enabled, false otherwise.
 */
bool VasdArgParse::IsDynamicAffinityAvailable()
{
    std::ifstream file(PROC_CMDLINE);
    if (!file.is_open()) {
        throw std::runtime_error("Open file failed. path=" + PROC_CMDLINE.string());
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(DYNAMIC_AFFINITY_ENABLE_KEY) != std::string::npos) {
            return true;
        }
    }
    return false;
}
} // namespace vas::sched

namespace vas::cli::reg {
using namespace vas::cli::framework;
using namespace vas::sched;

static const std::string SMT = "smt";
static const std::string SCHED_POLICY = "sched-policy";
static const std::string DYNAMIC_AFFINITY_UTIL_THRESH = "dynamic-util-thresh";
static const std::string SKIPPED_CPU_SET = "skip-cpuset";
static const std::string RANGE_AFFINITY = "range-affinity";
static const std::string DESC_FOR_SET_SERVER_CONFIG = "start server port with config.";
static const int DYNAMIC_AFFINITY_UTIL_THRESH_MAX_VALUE = 100;
static const int DYNAMIC_AFFINITY_UTIL_THRESH_MIN_VALUE = 0;

static const std::string SMT_DES =
    "CPU Allocation Granularity: A boolean parameter that determines core allocation strategy, defaulting to"
    " hyperthread-level allocation when set to true, and physical core-level allocation when set to false.";
static const std::string SCHED_POLICY_DES =
    "Scheduling Policy Configuration: A parameter specifying VCPU thread affinity mode, where dynamicAffinity enables"
    " dynamic CPU affinity and affinity enables static CPU core binding, with dynamicAffinity set as the default mode.";
static const std::string DYNAMIC_AFFINITY_UTIL_THRESH_DES =
    "Dynamic Affinity CPU Utilization Threshold: An integer parameter ranging from 0 to 100, defining the CPU usage"
    " threshold for dynamic affinity mode, with a default value of 85.";
static const std::string SKIPPED_CPU_SET_DES =
    "Management Core Skipping Configuration: A string-type parameter that defines the CpuSet to be bypassed when"
    " allocating CPUs for VCPU, ignoring the CPUs specified in skippedCpuSet, with a default value of \"\""
    " (no skipped CpuSet), \"-\" and \",\" can be used for express the CpuSet range, for example: \"0-1,6-8\".";
static const std::string RANGE_AFFINITY_DES =
    "CPU Reallocation Scope: A boolean parameter determining the CPU reallocation strategy, where true enables CPU"
    " reallocation for all VMs with core binding, and false limits reallocation to VMs bound by NUMA range, with"
    " true set as the default value.";
static const std::string SUCCESS_EXECUTE = "Success to set config";
static const std::string FAILED_EXECUTE = "Failed to execute command";

inline VasCliSdkResult PromptReply(const std::string &str)
{
    return {VasCliSdkResultType::NORMAL, str, {}};
}

/**
 * Set server configuration based on provided parameters.
 * @param params The configuration parameters.
 * @return The result of the configuration operation.
 */
VasCliSdkResult CliSetServerConfFunc(const std::map<std::string, std::string> &params)
{
    auto smtIter = params.find(SMT);
    auto schedPolicyIter = params.find(SCHED_POLICY);
    auto dynamicAffinityUtilThreshIter = params.find(DYNAMIC_AFFINITY_UTIL_THRESH);
    auto skippedCpuSetIter = params.find(SKIPPED_CPU_SET);
    auto rangeAffinityIter = params.find(RANGE_AFFINITY);

    if (smtIter != params.end()) {
        VasdArgParse::smt = (smtIter->second == "true" || smtIter->second == "1") &&
                            !(smtIter->second == "false" || smtIter->second == "0");
    }
    if (schedPolicyIter != params.end()) {
        VasdArgParse::schedPolicy = schedPolicyIter->second;
    }
    if (dynamicAffinityUtilThreshIter != params.end()) {
        try {
            int value = std::stoi(dynamicAffinityUtilThreshIter->second);
            if (value < DYNAMIC_AFFINITY_UTIL_THRESH_MIN_VALUE || value > DYNAMIC_AFFINITY_UTIL_THRESH_MAX_VALUE) {
                std::cerr << "Invalid value for dynamicAffinityUtilThresh: " << value << ". Expected range: 0-100."
                          << std::endl;
                return PromptReply(FAILED_EXECUTE);
            }
            VasdArgParse::dynamicAffinityUtilThresh = value;
        } catch (const std::invalid_argument &e) {
            std::cerr << "Invalid argument for dynamicAffinityUtilThresh: " << dynamicAffinityUtilThreshIter->second
                      << std::endl;
            return PromptReply(FAILED_EXECUTE);
        } catch (const std::out_of_range &e) {
            std::cerr << "Value out of range for dynamicAffinityUtilThresh: " << dynamicAffinityUtilThreshIter->second
                      << std::endl;
            return PromptReply(FAILED_EXECUTE);
        }
    }
    if (skippedCpuSetIter != params.end()) {
        VasdArgParse::skippedCPUSet = skippedCpuSetIter->second;
    }
    if (rangeAffinityIter != params.end()) {
        VasdArgParse::rangeAffinity = (rangeAffinityIter->second == "true" || rangeAffinityIter->second == "1") &&
                                      !(rangeAffinityIter->second == "false" || rangeAffinityIter->second == "0");
    }
    std::cout << "SMT: " << std::boolalpha << vas::sched::VasdArgParse::smt << std::endl;
    std::cout << "Schedule Policy: " << vas::sched::VasdArgParse::schedPolicy << std::endl;
    std::cout << "Dynamic Affinity Util Threshold: " << vas::sched::VasdArgParse::dynamicAffinityUtilThresh
              << std::endl;
    std::cout << "Range Affinity: " << vas::sched::VasdArgParse::rangeAffinity << std::endl;
    std::cout << "Skipped CPU Set: " << vas::sched::VasdArgParse::skippedCPUSet << std::endl;
    return PromptReply(SUCCESS_EXECUTE);
}

/**
 * Build and return the server configuration command information.
 * @return The server configuration command info.
 */
VasCliSdkCmdInfo SetServerConfig()
{
    SdkCmdInfoBuilder builder;
    builder.SetCommand("start")
        .SetType("server")
        .SetDesc(DESC_FOR_SET_SERVER_CONFIG)
        .AddParam("smt", SMT.c_str(), SMT_DES.c_str())
        .AddParam("sp", SCHED_POLICY.c_str(), SCHED_POLICY_DES.c_str())
        .AddParam("daut", DYNAMIC_AFFINITY_UTIL_THRESH.c_str(), DYNAMIC_AFFINITY_UTIL_THRESH_DES.c_str())
        .AddParam("scs", SKIPPED_CPU_SET.c_str(), SKIPPED_CPU_SET_DES.c_str())
        .AddParam("ra", RANGE_AFFINITY.c_str(), RANGE_AFFINITY_DES.c_str())
        .SetVasCliSdkCmdFun(CliSetServerConfFunc);
    return builder.Build();
}

/**
 * Register the server module SDK commands.
 */
void RegisterServerModuleSDK()
{
    std::vector<VasCliSdkCmdInfo> demo;
    demo.emplace_back(std::move(SetServerConfig()));
    VasCliParse::VasCliRegisterSdkCmdInfo(demo);
}
} // namespace vas::cli::reg
