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

#ifndef LOGGER_H
#define LOGGER_H

#include <filesystem>
#include <fstream>
#include <mutex>

#include "def.h"
#include "error.h"

namespace vas::common {
namespace fs = std::filesystem;

// log macro definition
#define LOG_DEBUG(msg) vas::common::Logger::Instance().Log(vas::common::Level::DEBUG, msg, __FILE__, __LINE__, __func__)
#define LOG_INFO(msg) vas::common::Logger::Instance().Log(vas::common::Level::INFO, msg, __FILE__, __LINE__, __func__)
#define LOG_WARN(msg) \
    vas::common::Logger::Instance().Log(vas::common::Level::WARNING, msg, __FILE__, __LINE__, __func__)
#define LOG_ERROR(msg) vas::common::Logger::Instance().Log(vas::common::Level::ERROR, msg, __FILE__, __LINE__, __func__)

// log config constant
const std::string LOGPATH = "/var/log/vas";
const std::string LOGFILE = "vasd.log";
constexpr uint8_t MAX_LOGFILE = 5;
constexpr int WIDTH = 3;
constexpr uint64_t MAX_LOGFILESIZE = 200 * SPACE_1M;

/**
 * log output type
 */
enum class OutputType
{
    NONE = 0,
    STDOUT,
    FILE,
};

/**
 * log level
 */
enum class Level
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger &Instance()
    {
        static Logger logger;
        return logger;
    }
    VasRet Init(const std::string &logPath, const std::string &logFile, const size_t &maxSize, const int &maxFiles,
                const OutputType &outputType);
    void Log(const Level &level, const std::string &message, const char *file, int line, const char *func);

private:
    Logger() = default;
    ~Logger()
    {
        if (file_.is_open()) {
            file_.close();
        }
    }
    std::mutex mutex_{};
    std::string logPath_{};
    std::string logFile_{};
    std::string logFilePath_{};
    uintmax_t maxSize_{};
    uintmax_t curSize_{};
    int maxFiles_{};
    Level logLevel_ = Level::INFO;
    OutputType outputType_{};
    std::ofstream file_{};
    static std::string LevelToStr(const Level &level);
    VasRet RotateCheck(const bool &forceOpen = false);
    VasRet RotateFiles() const;
};
} // namespace vas::common

#endif // LOGGER_H
