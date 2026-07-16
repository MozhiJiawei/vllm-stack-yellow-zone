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

#include "qemu_isolate_tuner.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"
#include "utils.h"

const std::string_view DATA_PATH = "/var/ubs-opt/data/";
const std::string_view DATA_FILE_NAME = "data.json";
constexpr const double TUNE_TRIGGER_PERCENT = 0.9;
constexpr const int NOT_FOUND = -1;

int maxQemuMvcount = -1;
int QemuIsolTuner::interval;

std::string QemuIsolTuner::name() const
{
    return "QEMU Process Isolation";
}

std::string QemuIsolTuner::category() const
{
    return "CPU BOUND";
}

std::string QemuIsolTuner::principle() const
{
    return "The QEMU process frequently switches CPUs, resulting in significant virtualization "
           "overhead.";
}

std::string QemuIsolTuner::advice() const
{
    return "Enable QEMU process isolation. physical machine and virtual machine topology synchronization in "
           "pass-through scenarios.";
}

bool QemuIsolTuner::check()
{
    EBPF_LOG_INFO("Checking QemuiSolate tunner.");
    ResultCode res = checkApply();
    if (res == ResultCode::SUCCESS) {
        EBPF_LOG_INFO("QemuIsolate configuration has been set up.");
        return false;
    } else if (res == ResultCode::ERROR) {
        isLastCheckSuccess = false;
        return true;
    }
    EBPF_LOG_INFO("QemuIsolate configuration has not been set up. ");
    isLastCheckSuccess = true;
    findLastInfer();
    if (maxQemuMvcount < static_cast<int>(interval * TUNE_TRIGGER_PERCENT)) {
        EBPF_LOG_INFO("QemuIsolate configuration is not needed.");
        return false;
    }
    EBPF_LOG_INFO("QemuIsolate configuration is needed.");
    return true;
}

void QemuIsolTuner::apply()
{
    std::cout << "1. On the physical machine, query the process ID of QEMU." << std::endl
              << "2. Set the CPU binding for QEMU using \"taskset -cp CPU_ID QEMU_ID\","
                 "where CPU_ID should be set to a CPU not allocated to the virtual machine. "
              << std::endl;
}

QemuIsolTuner::ResultCode QemuIsolTuner::checkApply()
{
    const rapidjson::Value vmname_value = utils::getConfigValue("vm_name");
    if (vmname_value.IsNull()) {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Optimizer parse config failed, can not find vm_name.");
        return ResultCode::ERROR;
    }
    std::string vm_name = vmname_value.GetString();

    static const char *cmd = "vmtop -b -n 1";
    CmdExecutor executor;
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        return ResultCode::ERROR;
    }

    std::istringstream iss(result.second);
    std::string line;

    int pid_pos = NOT_FOUND;
    unsigned int pid = 0;
    while (std::getline(iss, line)) {
        if (line.find("VM/task-name") != std::string::npos) {
            std::vector<std::string> tokens = splitBySpace(line);
            pid_pos = findPIndex(tokens);
            if (pid_pos == -1) {
                EBPF_LOG_ERROR("QemuIsolTuner::checkApply run vmtop -b -n 1 failed.");
                isLastCheckSuccess = false;
                return ResultCode::ERROR;
            }
        }
        if (line.find(vm_name) != std::string::npos && pid_pos != NOT_FOUND) {
            std::vector<std::string> tokens = splitBySpace(line);
            pid = static_cast<unsigned int>(std::stoi(tokens[pid_pos]));
            break;
        }
    }
    std::string tasksetCmd = "taskset -p " + std::to_string(pid);
    result = executor.runCommand("taskset -p " + std::to_string(pid));
    if (!result.first) {
        isLastCheckSuccess = false;
        return ResultCode::ERROR;
    }

    auto isApply = isAllF(result.second);
    if (isApply == ResultCode::SUCCESS) {
        return ResultCode::FALSE;
    } else if (isApply == ResultCode::ERROR) {
        isLastCheckSuccess = false;
        return ResultCode::ERROR;
    }
    return ResultCode::SUCCESS;
}

int QemuIsolTuner::findPIndex(const std::vector<std::string> &tokens)
{
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "PID") {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QemuIsolTuner::ResultCode QemuIsolTuner::isAllF(const std::string &line)
{
    size_t colon_pos = line.find(":");
    if (colon_pos == std::string::npos) {
        EBPF_LOG_ERROR("QemuIsolTuner::isAllF run taskset command failed.");
        return ResultCode::ERROR;
    }
    std::string after_colon = line.substr(colon_pos + 1);
    after_colon.erase(std::remove_if(after_colon.begin(), after_colon.end(), ::isspace), after_colon.end());

    if (std::all_of(after_colon.begin(), after_colon.end(), [](char c) { return c == 'f'; })) {
        return ResultCode::SUCCESS;
    } else {
        return ResultCode::FALSE;
    }
}

std::vector<std::string> QemuIsolTuner::splitBySpace(const std::string &line)
{
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // Automatically skip spaces
        tokens.push_back(token);
    }
    return tokens;
}

std::ifstream QemuIsolTuner::openDataFile(std::string_view filename)
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

void QemuIsolTuner::closeDataFile(std::string_view filename, std::ifstream file)
{
    char *dataPath = realpath(std::string().append(DATA_PATH).append(filename).append(".bak").c_str(), nullptr);
    const std::string renamedPath(dataPath);
    file.close();
    std::filesystem::remove(renamedPath);
    free(dataPath);
}

void QemuIsolTuner::parseHostData(const std::string &rawJson)
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
        if (host_data.HasMember("qemu_migration_count") && host_data["qemu_migration_count"].IsInt()) {
            int migration_count = host_data["qemu_migration_count"].GetInt();
            maxQemuMvcount = std::max(maxQemuMvcount, migration_count);
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

void QemuIsolTuner::findLastInfer()
{
    EBPF_LOG_INFO("Checking Qemu isolate tunner.");
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
