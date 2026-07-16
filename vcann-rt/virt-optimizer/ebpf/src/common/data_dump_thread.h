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

#ifndef DATADUMPTHREAD_H
#define DATADUMPTHREAD_H

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "data_struct.h"

constexpr unsigned dumpIntervalSec = 60 * 60 * 12; // Forceful dump after exceeding this interval

class DataDumpThread {
public:
    explicit DataDumpThread(const std::string &dataFilePath, std::shared_ptr<MutexContext> ctx,
                            int intervalSec = dumpIntervalSec);
    ~DataDumpThread();

    DataDumpThread(const DataDumpThread &) = delete;
    DataDumpThread &operator=(const DataDumpThread &) = delete;

    void start();
    void stop();

private:
    void run();
    bool isOversize();
    void doDumpData();
    std::string generateDumpFileName();

    std::string dataPath;
    std::string dataDir;
    std::string dataBaseName;
    int intervalSeconds;

    std::thread monitorThread;
    std::atomic<bool> stopFlag;

    std::mutex mtx;
    std::condition_variable condVar;

    std::shared_ptr<MutexContext> context;

    static constexpr size_t DUMP_FILE_SIZE_THRESHOLD = 20;
};

#endif
