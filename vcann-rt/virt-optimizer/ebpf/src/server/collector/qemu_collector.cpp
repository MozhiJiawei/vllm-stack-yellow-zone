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

#include "qemu_collector.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "log/ebpf_logger_macros.h"

namespace fs = std::filesystem;

void QEMUCollector::launch(std::string vm_name, double timeout, std::atomic<int> &res)
{
    QEMUCollectorThread_ = std::make_unique<std::thread>(&QEMUCollector::monitor_qemu_threads, this, std::ref(vm_name),
                                                         timeout, std::ref(res));
}

QEMUCollector &QEMUCollector::getInstance()
{
    static QEMUCollector instance;
    return instance;
}

void QEMUCollector::monitor_qemu_threads(std::string vm_name, double timeout, std::atomic<int> &res)
{
    auto start_time = std::chrono::system_clock::now();
    // Start the continuous monitoring process
    std::string cmd = "vmtop -H -b -d 1";

    std::map<std::string, int> history; // {tid: last_cpu}

    // Open child process
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        EBPF_LOG_ERROR("Failed to open pipe");
        return;
    }

    while (true) {
        char buffer[512];
        if (!fgets(buffer, sizeof(buffer), pipe)) {
            break; // Process ends
        }

        std::string line(buffer);

        if (line.find("VM/task-name") != std::string::npos && cpu_pos == -1) {
            std::vector<std::string> tokens = splitBySpace(line);
            cpu_pos = findPIndex(tokens);
            history[vm_name] = -1;
        }

        if (line.find(vm_name) != std::string::npos && cpu_pos >= 0) {
            std::vector<std::string> tokens = splitBySpace(line);
            int cpu_id = std::stoi(tokens[static_cast<size_t>(cpu_pos)]);
            if (history[vm_name] != cpu_id) {
                history[vm_name] = cpu_id;
                mv_count++;
            }
            continue;
        }

        auto stop_time = std::chrono::system_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(stop_time - start_time);
        if (elapsed_time.count() > timeout) {
            res.store(mv_count, std::memory_order_release);
            mv_count = 0;
            start_time = std::chrono::system_clock::now();
            elapsed_time = std::chrono::seconds(0);
        }
    }
    pclose(pipe);
}

std::vector<std::string> QEMUCollector::splitBySpace(const std::string &line)
{
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) { // Automatically skip spaces
        tokens.push_back(token);
    }
    return tokens;
}

int QEMUCollector::findPIndex(const std::vector<std::string> &tokens)
{
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "P") {
            return static_cast<int>(i); // Return index
        }
    }
    return -1; // Not found
}