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

#include "monitor.h"

#include <fstream>

#include <sys/statvfs.h>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"

using u64 = unsigned long long;
static constexpr const char *DISK_MONITE_PATH = "/var";          // Check the disk space in this directory.
static constexpr unsigned int NORMAL_MONITE_INTERVAL_SEC = 10;   // Detecting interval under normal conditions
static constexpr unsigned int OVERLOAD_MONITE_INTERVAL_SEC = 60; // Detecting interval under overloaded conditions
static constexpr unsigned int BUFFER_SIZE = 255;                 // Buffer for accepting command output
static constexpr unsigned int TO_PERCENT = 100;                  // Convert decimal to percentage
static constexpr unsigned int DISK_LIMIT = 85;                   // Disk overload Threshold
static constexpr unsigned int RESTORE_LIMIT = 10;     // Resume threshold = (overload threshold) - RESTORE_LIMIT
static constexpr unsigned int OVERLOAD_THRESHOLD = 3; // Overload times == 3 -> stop, resume times == 0 -> start

void Monitor::launch()
{
    guestName = getGuestName();
    if (guestName.empty()) {
        EBPF_LOG_WARN("Monitor failed to start, error code = 2003.");
        return;
    }
    monitorThread_ = std::make_unique<std::thread>(std::thread(&Monitor::mainLoop, this));
}

[[noreturn]] void Monitor::mainLoop()
{
    CmdExecutor executor(guestName);
    bool stoppedByAuto = false;
    while (true) {
        if (!stoppedByAuto && !checkDisk(DISK_LIMIT) && ++overloadTimes == OVERLOAD_THRESHOLD) {
            EBPF_LOG_WARN("The collector has shut down. Error code: 1001.");
            stoppedByAuto = true;
            if (executor.runCommand("ubs-opt stop_ebpf").first) {
                EBPF_LOG_INFO("The collector successfully closed.");
            } else {
                EBPF_LOG_WARN("Failed to shut down the collector. It may have been manually stopped.");
            }
        } else if (stoppedByAuto && checkDisk(DISK_LIMIT - RESTORE_LIMIT) && --overloadTimes == 0) {
            EBPF_LOG_INFO("Collector starts to run.");
            stoppedByAuto = false;
            if (executor.runCommand("ubs-opt start_ebpf").first) {
                EBPF_LOG_INFO("The collector successfully started.");
            } else {
                EBPF_LOG_WARN("Failed to start the collector. It may have been manually started.");
            }
        }
        if (!stoppedByAuto) {
            std::this_thread::sleep_for(std::chrono::seconds(NORMAL_MONITE_INTERVAL_SEC));
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(OVERLOAD_MONITE_INTERVAL_SEC));
        }
    }
}

std::string Monitor::getGuestName()
{
    FILE *pipe = popen("virsh list", "r");
    if (!pipe) {
        EBPF_LOG_ERROR("Failed to retrieve vm information");
        return "";
    }

    char buffer[BUFFER_SIZE];
    std::string name;
    bool alreadyGet = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        unsigned int i = 0;
        while (i < BUFFER_SIZE && buffer[i] == ' ') {
            i += 1;
        }
        if (i == BUFFER_SIZE || buffer[i] < '0' || buffer[i] > '9') {
            continue;
        }
        if (alreadyGet) {
            EBPF_LOG_ERROR("Multiple virtual machines exist. The monitor will exit.");
            name = "";
            break;
        }
        std::istringstream iss(buffer);
        // The first value is not needed; simply overwrite it.
        iss >> name >> name;
        alreadyGet = true;
    }
    pclose(pipe);
    for (auto c : name) {
        if (!isalpha(c) && !isdigit(c) && (c != '_') && (c != '-') && c != '.') {
            EBPF_LOG_ERROR("The virtual machine name is invalid."
                           "Only characters, numbers, '-', '.' and '_' are allowed.");
            name = "";
            break;
        }
    }
    return name;
}

bool Monitor::checkDisk(unsigned int limitPercent)
{
    struct statvfs vfs {
    };
    if (statvfs(DISK_MONITE_PATH, &vfs) != 0) {
        // If the disk information cannot be read, this item will not be checked.
        EBPF_LOG_WARN("Failed to obtain disk space.");
        return true;
    }
    u64 totalBytes = vfs.f_blocks * vfs.f_frsize;
    u64 usedBytes = totalBytes - vfs.f_bavail * vfs.f_frsize;
    if ((totalBytes <= 0) || (usedBytes * TO_PERCENT <= limitPercent * totalBytes)) {
        return true;
    }
    EBPF_LOG_DEBUG("Disk space is running low. Total space: " + std::to_string(totalBytes) +
                   ", Used space: " + std::to_string(usedBytes));
    return false;
}

Monitor &Monitor::getInstance()
{
    static Monitor monitor;
    return monitor;
}
