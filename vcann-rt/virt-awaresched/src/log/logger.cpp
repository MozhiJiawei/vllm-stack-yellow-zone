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

#include "logger.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include <sys/stat.h>

#include "error.h"

namespace vas::common {
constexpr mode_t DIR_PERM = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t FILE_PERM = S_IRUSR | S_IWUSR | S_IRGRP;
constexpr mode_t ARCHIVE_PERM = S_IRUSR | S_IRGRP;
/**
 * Initialization
 * @param logPath log path
 * @param logFile
 * @param maxSize Maximum number of log connections
 * @param maxFiles Maximum number of log files
 * @param outputType Log output type, screen printing or log printing
 */
VasRet Logger::Init(const std::string &logPath, const std::string &logFile, const size_t &maxSize, const int &maxFiles,
                    const OutputType &outputType)
{
    std::lock_guard lock(mutex_);
    logPath_ = logPath;
    logFile_ = logFile;
    logFilePath_ = (fs::path(logPath_) / logFile_).string();
    maxSize_ = maxSize;
    maxFiles_ = maxFiles;
    outputType_ = outputType;

    if (maxFiles_ <= 1) {
        maxFiles_ = MAX_LOGFILE;
    }
    try {
        if (outputType != OutputType::FILE) {
            return VAS_OK;
        }
        // check and create dir
        if (!fs::exists(logPath_)) {
            fs::create_directories(logPath_);
            chmod(logPath_.c_str(), DIR_PERM);
        }
        // Check and create log file
        if (!logFilePath_.empty()) {
            if (RotateCheck(true) != VAS_OK) {
                return VAS_ERROR;
            }
        }
        return VAS_OK;
    } catch (const fs::filesystem_error &e) {
        std::cerr << "Failed to create log directory: " << e.what() << std::endl;
        return VAS_ERROR;
    }
}

/**
 * Print log
 * @param level log level
 * @param message log info
 * @param file invoked file
 * @param line invoked line
 * @param func func name
 */
void Logger::Log(const Level &level, const std::string &message, const char *file, int line, const char *func)
{
    std::lock_guard lock(mutex_);
    if (level < logLevel_) {
        return;
    }

    switch (outputType_) {
        case OutputType::NONE: {
            break;
        }
        case OutputType::STDOUT: {
            std::cout << message << std::endl;
            break;
        }
        case OutputType::FILE: {
            // Generate log header
            const auto now = std::chrono::system_clock::now();
            const auto t = std::chrono::system_clock::to_time_t(now);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::stringstream ss;
            ss << "[" << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0')
               << std::setw(WIDTH) << ms.count() << "][" << LevelToStr(level) << "]["
               << fs::path(file).filename().string() << ":" << line << "][" << std::string(func) << "]["
               << std::this_thread::get_id() << "]" << message << "\n";
            if (!logFilePath_.empty()) {
                if (RotateCheck() != VAS_OK) {
                    std::cerr << "Failed to Check if need rotate" << std::endl;
                    break;
                }
                file_ << ss.str();
                file_.flush();
                curSize_ += ss.str().size();
            }
            break;
        }
        default: {
            std::cout << "Not support output type: " << static_cast<uint64_t>(outputType_);
            break;
        }
    }
}

/**
 * Converting level to string
 * @param level log level
 * @return
 */
std::string Logger::LevelToStr(const Level &level)
{
    switch (level) {
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARNING:
            return "WARN";
        case Level::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

/**
 * Check if need rotate
 * @param forceOpen If the log file is not currently open, this parameter determines
 * whether to allow the rotation logic to proceed.
 * - If true, the log file will be opened or reopened, and the rotation check will be performed.
 * - If false, the rotation check will only be performed if the log file is already open.
 */
VasRet Logger::RotateCheck(const bool &forceOpen)
{
    if (!file_.is_open() || forceOpen) {
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(logFilePath_, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath_ << std::endl;
            return VAS_ERROR;
        }
        if (chmod(logFilePath_.c_str(), FILE_PERM) != 0) {
            std::cerr << "Failed to chmod file: " << logFilePath_ << std::endl;
            file_.close();
            return VAS_ERROR;
        }
        curSize_ = fs::file_size(logFilePath_);
    }

    if (curSize_ >= maxSize_) {
        RotateFiles();
        file_.close();
        file_.open(logFilePath_);
        if (!file_.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath_ << std::endl;
            return VAS_ERROR;
        }
        if (chmod(logFilePath_.c_str(), FILE_PERM)) {
            std::cerr << "Failed to chmod file: " << logFilePath_ << std::endl;
            file_.close();
            return VAS_ERROR;
        }
        curSize_ = 0;
    }
    return VAS_OK;
}

/**
 * Wrap Log File
 */
VasRet Logger::RotateFiles() const
{
    // delete the oldest file
    std::string oldestFile = logFilePath_ + "." + std::to_string(maxFiles_);
    if (fs::exists(oldestFile)) {
        if (!fs::remove(oldestFile)) {
            std::cerr << "Failed to remove file: " << oldestFile << std::endl;
            return VAS_ERROR;
        }
    }

    // rename file
    for (int i = maxFiles_ - 1; i >= 1; --i) {
        std::string src = logFilePath_ + "." + std::to_string(i);
        std::string dst = logFilePath_ + "." + std::to_string(i + 1);
        if (fs::exists(src)) {
            fs::rename(src, dst);
            if (chmod(dst.c_str(), ARCHIVE_PERM)) {
                std::cerr << "Failed to chmod file: " << logFilePath_ << std::endl;
                return VAS_ERROR;
            }
        }
    }

    std::string archiveFile = logFilePath_ + ".1";
    fs::rename(logFilePath_, archiveFile);
    if (chmod(archiveFile.c_str(), ARCHIVE_PERM)) {
        std::cerr << "Failed to chmod file: " << logFilePath_ << std::endl;
        return VAS_ERROR;
    }
    return VAS_OK;
}
} // namespace vas::common