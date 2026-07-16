/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace virt::logger {
enum class LoggerLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    Logger(LoggerLevel level, const char *file, const char *func, int line) noexcept;
    ~Logger() noexcept
    {
        Submit();
    }
    template <typename T>
    Logger &operator<<(const T &val)
    {
        ss_ << val;
        return *this;
    }

    void Submit();

private:
    static void RotateLogFile();

    LoggerLevel level_;
    const char *file_;
    const char *func_;
    int line_;

    pid_t pid_;
    uint64_t tid_;
    std::chrono::system_clock::time_point timestamp_;
    std::stringstream ss_;

    static constexpr const char *Basename(const char *path) noexcept;
    static inline uint64_t GetTid() noexcept;
    std::string FormatTime(const std::chrono::system_clock::time_point &tp);
};

void EnsureLogDir();
size_t GetFileSize(const char *path);
std::string NowFilename();
void SetFileMode(const char *path, mode_t mode);
void InitLogFile();
void CleanupOldRotateLogFile();
void CompressOldLogFile(const std::string &oldLogFile, const std::string &ts);

constexpr size_t MAX_LOG_SIZE = 50 * 1024 * 1024;
} // namespace virt::logger

#define LOG_DEBUG virt::logger::Logger(virt::logger::LoggerLevel::DEBUG, __FILE__, __FUNCTION__, __LINE__)
#define LOG_INFO virt::logger::Logger(virt::logger::LoggerLevel::INFO, __FILE__, __FUNCTION__, __LINE__)
#define LOG_WARN virt::logger::Logger(virt::logger::LoggerLevel::WARN, __FILE__, __FUNCTION__, __LINE__)
#define LOG_ERROR virt::logger::Logger(virt::logger::LoggerLevel::ERROR, __FILE__, __FUNCTION__, __LINE__)
#endif