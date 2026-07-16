/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "vcpu_isolate_tuner.h"

#include <cstdlib>
#include <iostream>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"

const std::string_view DATA_PATH = "/var/ubs-opt/data/";
const std::string_view DATA_FILE_NAME = "data.json";
constexpr const int TUNER_THRESHOLD = 5;
int maxPcpuPreemptCount = -1;
int VCPUIsolTuner::interval;

std::string VCPUIsolTuner::name() const
{
    return "Exclusive vCPU";
}

std::string VCPUIsolTuner::category() const
{
    return "CPU BOUND";
}

std::string VCPUIsolTuner::principle() const
{
    return "Frequent preemption of the physical machine's processes on the CPU allocated to the virtual machine leads "
           "to CPU-side bubbles or slow execution.";
}

std::string VCPUIsolTuner::advice() const
{
    return "Enable Exclusive vCPU. The vCPU can only be scheduled within the allocated virtual machine.";
}

bool VCPUIsolTuner::check()
{
    EBPF_LOG_INFO("Checking VCPUIsolTuner tunner.");
    ResultCode res = checkApply();
    if (res == ResultCode::SUCCESS) {
        EBPF_LOG_INFO("VCPUIsolTuner configuration has been set up.");
        return false;
    } else if (res == ResultCode::ERROR) {
        isLastCheckSuccess = false;
        return true;
    }
    EBPF_LOG_INFO("VCPUIsolTuner configuration has not been set up. ");
    isLastCheckSuccess = true;
    findLastInfer();
    if (maxPcpuPreemptCount < (interval * TUNER_THRESHOLD)) {
        EBPF_LOG_INFO("VCPUIsolTuner configuration is not needed.");
        return false;
    }
    EBPF_LOG_INFO("VCPUIsolTuner configuration is needed.");
    return true;
}

void VCPUIsolTuner::apply()
{
    std::cout << "Enable Exclusive vCPU." << std::endl;
}

VCPUIsolTuner::ResultCode VCPUIsolTuner::checkApply()
{
    const std::vector<std::string> isolationParams = {"isolcpus=", "nohz_full=", "rcu_nocbs="};

    static const char *cmd = "cat /proc/cmdline";
    CmdExecutor executor;
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        EBPF_LOG_ERROR("Failed to run command: cat /proc/cmdline");
        isLastCheckSuccess = false;
        return ResultCode::ERROR;
    }

    for (const auto &param : isolationParams) {
        if (result.second.find(param) == std::string::npos) {
            return ResultCode::FALSE;
        }
    }

    return ResultCode::SUCCESS;
}

std::ifstream VCPUIsolTuner::openDataFile(std::string_view filename)
{
    char *dataPath = realpath(std::string().append(DATA_PATH).append(filename).c_str(), nullptr);
    const std::string path(dataPath);
    const std::string renamedPath = path + ".bak";
    if (std::filesystem::exists(renamedPath)) {
        std::filesystem::remove(renamedPath);
    }
    std::filesystem::copy(path, renamedPath);
    std::ifstream file(renamedPath);
    free(dataPath);
    return file;
}

void VCPUIsolTuner::closeDataFile(std::string_view filename, std::ifstream file)
{
    char *dataPath = realpath(std::string().append(DATA_PATH).append(filename).append(".bak").c_str(), nullptr);
    const std::string renamedPath(dataPath);
    file.close();
    std::filesystem::remove(renamedPath);
    free(dataPath);
}

void VCPUIsolTuner::parseHostData(const std::string &rawJson)
{
    rapidjson::Document doc;
    rapidjson::ParseResult ok = doc.Parse(rawJson.c_str());
    if (!ok) {
        throw ::std::runtime_error("Not json format.");
    }
    if (!doc.HasMember("guest_name")) {
        throw ::std::runtime_error("Does not have 'guest_name'.");
    }

    if (doc.HasMember("data_table") && doc["data_table"].IsObject()) {
        const rapidjson::Value &host_data = doc["data_table"];
        if (host_data.HasMember("host_preempt_vmcore_count") && host_data["host_preempt_vmcore_count"].IsInt()) {
            int preempt_count = host_data["host_preempt_vmcore_count"].GetInt();
            maxPcpuPreemptCount = std::max(maxPcpuPreemptCount, preempt_count);
        }
    }

    if (!doc.HasMember("interval") || !doc["interval"].IsInt()) {
        throw ::std::runtime_error("Does not have 'interval' or 'interval' is not int.");
    }

    if (doc["interval"].GetInt() <= 0) {
        throw ::std::runtime_error("interval must be a positive integer, current value: " +
                                   std::to_string(doc["interval"].GetInt()));
    }

    interval = doc["interval"].GetInt();
}

void VCPUIsolTuner::findLastInfer()
{
    EBPF_LOG_INFO("Checking vcpu isolate tunner.");
    std::ifstream file;
    try {
        file = openDataFile(DATA_FILE_NAME);
        if (!file.is_open()) {
            isLastCheckSuccess = false;
            EBPF_LOG_ERROR("Failed to found data file: " + std::string(DATA_PATH) + std::string(DATA_FILE_NAME));
            return;
        }
    } catch (const std::exception &e) {
        isLastCheckSuccess = false;
        EBPF_LOG_WARN("Unable to open data file: " + std::string(e.what()));
        return;
    }
    std::string rawJson;
    time_t lastTimestamp = 0;
    while (std::getline(file, rawJson)) {
        try {
            parseHostData(rawJson);
        } catch (const std::exception &e) {
            continue;
        }
    }
    closeDataFile(DATA_FILE_NAME, std::move(file));
}
