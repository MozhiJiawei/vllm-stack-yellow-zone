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

#ifndef EBPF_LOGGER_H
#define EBPF_LOGGER_H

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace fs = std::filesystem;
constexpr size_t max_file_size = 20 * 1024 * 1024;
constexpr unsigned log_to_archive_threshold = 10;
inline constexpr const char *logDumpTimestampFormat = "%Y%m%d_%H%M%S";
inline constexpr const char *logFilenameTimestampFormat = "%Y-%m-%d %H:%M:%S";

class EbpfLogger {
public:
    enum class LogLevel
    {
        DEBUG = 0,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

    static EbpfLogger &getInstance();

    bool init(const std::string &logFile, LogLevel level = LogLevel::INFO, bool useSyslog = true,
              bool useConsole = false, size_t maxFileSize = max_file_size);

    void logMessage(LogLevel level, const std::string &message, const char *file, const char *function, int line);

    void cleanup();

    EbpfLogger(const EbpfLogger &) = delete;

    EbpfLogger &operator=(const EbpfLogger &) = delete;

    // For unit testing only, get the current log queue size
    size_t getLogQueueSizeForTest()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return logQueue_.size();
    }

    // For testing only, to determine if it is running
    bool isRunningForTest() const
    {
        return running_;
    }

private:
    EbpfLogger();

    ~EbpfLogger();

    void logThread();

    void dumpLogFile();

    void archiveLog();

    static std::string getTimestamp(const char *format);

    static constexpr std::string_view levelToString(LogLevel level);

    static std::string getThreadId();

    static void mkdirWithPermission(const fs::path &, fs::perms);

    std::string logFile_;
    std::string logDir_;
    std::string logBaseName_;
    LogLevel minLogLevel_;
    bool useSyslog_;
    bool useConsole_;
    size_t maxFileSize_;
    std::ofstream logFileStream_;

    std::queue<std::string> logQueue_;
    std::mutex mutex_;
    std::condition_variable condVar_;
    std::thread logWorkerThread_;
    std::atomic<bool> running_;

    static constexpr int MAX_LOG_FILES = log_to_archive_threshold;
};

#endif // EBPF_LOGGER_H
