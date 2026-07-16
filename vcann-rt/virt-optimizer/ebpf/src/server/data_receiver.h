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

#ifndef DATA_RECEIVER_H
#define DATA_RECEIVER_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data_struct.h"

constexpr unsigned BUFFER_THRESHOLD = 64;  // When the number of data entries exceeds the threshold, write to disk.
constexpr unsigned FLUSH_INTERVAL_SEC = 8; // If not written to disk within this time, directly write to disk.
const std::string data_output_path = "/var/ubs-opt/data/data.json";

class DataReceiver {
public:
    explicit DataReceiver(std::shared_ptr<MutexContext> ctx, size_t bufferSize = BUFFER_THRESHOLD,
                          int flushIntervalSec = FLUSH_INTERVAL_SEC, std::string outputPath = data_output_path);
    ~DataReceiver();

    DataReceiver(const DataReceiver &) = delete;
    DataReceiver &operator=(const DataReceiver &) = delete;

    void save(const std::string &dataJson);

private:
    void flushTimer();
    void writeToDisk();

    std::vector<std::string> jsonBuffer;
    size_t maxBufferSize;
    int flushInterval;
    std::string outputPath;

    std::shared_ptr<MutexContext> context;
    std::mutex mtx;

    std::condition_variable condVar;
    bool stopFlag;
    std::thread flushThread;
};

#endif
