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

#include "data_receiver.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include "log/ebpf_logger_macros.h"

namespace fs = std::filesystem;

DataReceiver::DataReceiver(std::shared_ptr<MutexContext> ctx, size_t bufferSize, int flushIntervalSec,
                           std::string outputPath)
    : context(std::move(ctx)),
      maxBufferSize(bufferSize),
      flushInterval(flushIntervalSec),
      outputPath(std::move(outputPath)),
      stopFlag(false)
{
    flushThread = std::thread(&DataReceiver::flushTimer, this);
}

DataReceiver::~DataReceiver()
{
    {
        std::unique_lock<std::mutex> lock(mtx);
        stopFlag = true;
        condVar.notify_all();
    }
    if (flushThread.joinable()) {
        flushThread.join();
    }
    writeToDisk();
}

void DataReceiver::save(const std::string &dataJson)
{
    std::unique_lock<std::mutex> lock(mtx);
    jsonBuffer.push_back(dataJson);

    if (jsonBuffer.size() >= maxBufferSize) {
        writeToDisk();
    }
}

void DataReceiver::flushTimer()
{
    std::unique_lock<std::mutex> lock(mtx);
    while (!stopFlag) {
        if (condVar.wait_for(lock, std::chrono::seconds(flushInterval), [this]() { return stopFlag; })) {
            break;
        }
        writeToDisk();
    }
}

void DataReceiver::writeToDisk()
{
    if (!context) { // Avoiding null pointer exceptions
        EBPF_LOG_ERROR("Error: pointer is null!");
        return;
    }
    std::lock_guard<std::mutex> lock(context->fileMutex);

    if (jsonBuffer.empty()) {
        return;
    }
    fs::path pathObj(outputPath);
    fs::path parentDir = pathObj.parent_path();
    if (!parentDir.empty() && !fs::exists(parentDir)) {
        fs::path curDir;
        for (const auto &part : parentDir) {
            curDir /= part;
            if (!fs::exists(curDir)) {
                fs::create_directories(curDir);
                fs::permissions(curDir, fs::perms::owner_all);
            }
        }
    }

    bool fileExits = fs::exists(pathObj);
    std::ofstream ofs(outputPath, std::ios::app);
    if (!ofs.is_open()) {
        EBPF_LOG_ERROR("Failed to open file: " + outputPath);
        return;
    }
    if (fileExits ^ fs::exists(pathObj)) {
        fs::permissions(outputPath, fs::perms::owner_read | fs::perms::owner_write);
    }
    for (const auto &jsonLine : jsonBuffer) {
        EBPF_LOG_DEBUG("Flushing " + jsonLine);
        ofs << jsonLine << "\n";
    }
    jsonBuffer.clear();
}
