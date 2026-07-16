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
#include "logger.h"

#include <dirent.h>
#include <sys/syscall.h>
#include <algorithm>
#include <vector>

namespace virt::logger {
constexpr size_t MAX_ROTATE_FILES = 5;
constexpr int BUFER_SIZE = 32;
constexpr int MILLI_WIDTH = 3;
constexpr int CHANGE_TO_MS = 1000;
constexpr mode_t CUR_LOG_MODE = 0640;
constexpr mode_t ROT_LOG_MODE = 0440;
constexpr mode_t LOG_DIR_MODE = 0750;

constexpr char LOG_DIR[] = "/var/log/ubs-virt-ovs";
constexpr char LOG_FILE[] = "/var/log/ubs-virt-ovs/ubs-virt-ovs.log";
constexpr char TIME_FMT[] = "%Y%m%d_%H%M%S";

struct LogFileInfo {
    std::string name;
    time_t mtime;
};

std::ofstream &LogFile()
{
    static std::ofstream ofs;
    return ofs;
}

std::mutex &LogMutex()
{
    static std::mutex m;
    return m;
}

void EnsureLogDir()
{
    struct stat st {
    };
    if (stat(LOG_DIR, &st) != 0) {
        mkdir(LOG_DIR, LOG_DIR_MODE);
    }
}

size_t GetFileSize(const char *path)
{
    struct stat st {
    };
    if (stat(path, &st) != 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
}

std::string NowFilename()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&tt, &tm);

    char buf[BUFER_SIZE];
    size_t len = strftime(buf, sizeof(buf), TIME_FMT, &tm);
    if (len == 0) {
        return "00000000_000000";
    }
    return buf;
}

void SetFileMode(const char *path, mode_t mode)
{
    chmod(path, mode);
}

void InitLogFile()
{
    auto &ofs = LogFile();
    if (ofs.is_open()) {
        return;
    }

    EnsureLogDir();
    ofs.open(LOG_FILE, std::ios::out | std::ios::app);
    if (!ofs.is_open()) {
        return;
    }

    SetFileMode(LOG_FILE, CUR_LOG_MODE);
}

void CleanupOldRotateLogFile()
{
    DIR *dir = opendir(LOG_DIR);
    if (!dir) {
        return;
    }

    constexpr char rotateSuffix[] = ".tar.gz";
    constexpr char rotatePrefix[] = "virt_ovs_";
    constexpr size_t suffixLen = sizeof(rotateSuffix) - 1;
    constexpr size_t prefixLen = sizeof(rotatePrefix) - 1;

    std::vector<LogFileInfo> files;
    struct dirent *ent;

    while ((ent = readdir(dir)) != nullptr) {
        std::string name(ent->d_name);
        if (name.size() < prefixLen + suffixLen) {
            continue;
        }
        if (name.compare(0, prefixLen, rotatePrefix) != 0) {
            continue;
        }
        if (name.compare(name.size() - suffixLen, suffixLen, rotateSuffix) != 0) {
            continue;
        }

        std::string fullPath = std::string(LOG_DIR) + "/" + name;
        struct tm tm {
        };
        if (strptime(name.c_str() + prefixLen, TIME_FMT, &tm) == nullptr) {
            continue;
        }
        const time_t t = mktime(&tm);
        if (t == -1) {
            continue;
        }
        files.push_back({name, t});
    }

    closedir(dir);
    if (files.size() <= MAX_ROTATE_FILES) {
        return;
    }

    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.mtime < b.mtime; });
    for (size_t i = 0; i < files.size() - MAX_ROTATE_FILES; ++i) {
        std::string fullPath = std::string(LOG_DIR) + "/" + files[i].name;
        unlink(fullPath.c_str());
    }
}

void CompressOldLogFile(const std::string &oldLogFile, const std::string &ts)
{
    std::string tarFile = std::string(LOG_DIR) + "/virt_ovs_" + ts + ".tar.gz";
    std::string cmd =
        "tar -czf" + tarFile + " -C " + LOG_DIR + " " + oldLogFile.substr(oldLogFile.find_last_of('/') + 1);
    system(cmd.c_str());
    unlink(oldLogFile.c_str());
    SetFileMode(tarFile.c_str(), ROT_LOG_MODE);

    CleanupOldRotateLogFile();
}

void Logger::RotateLogFile()
{
    if (GetFileSize(LOG_FILE) < MAX_LOG_SIZE) {
        return;
    }

    auto &ofs = LogFile();
    std::string ts = NowFilename();
    std::string oldLogFile;

    {
        std::string newLogFile = std::string(LOG_DIR) + "/virt_ovs_" + ts + ".log";
        std::ofstream newOfs(newLogFile, std::ios::out | std::ios::app);
        if (!newOfs.is_open()) {
            return;
        }
        std::swap(ofs, newOfs);
        oldLogFile = LOG_FILE;
        SetFileMode(LOG_FILE, CUR_LOG_MODE);
    }

    std::thread(CompressOldLogFile, oldLogFile, ts).detach();
}

Logger::Logger(LoggerLevel level, const char *file, const char *func, int line) noexcept
    : level_(level),
      file_(Basename(file)),
      func_(func),
      line_(line),
      timestamp_(std::chrono::system_clock::now()),
      pid_(getpid()),
      tid_(GetTid())
{
}

constexpr const char *Logger::Basename(const char *path) noexcept
{
    if (!path) {
        return "";
    }
    const char *lastSlash = strrchr(path, '/');
    return lastSlash ? lastSlash + 1 : path;
}

inline uint64_t Logger::GetTid() noexcept
{
    return static_cast<uint64_t>(syscall(SYS_gettid));
}

std::string Logger::FormatTime(const std::chrono::system_clock::time_point &tp)
{
    auto tt = std::chrono::system_clock::to_time_t(tp);

    std::tm tm{};
    localtime_r(&tt, &tm);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % CHANGE_TO_MS;
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setw(MILLI_WIDTH) << std::setfill('0') << ms.count()
        << " +08:00";
    return oss.str();
}

void Logger::Submit()
{
    static const char *levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    std::ostringstream oss;
    oss << FormatTime(timestamp_) << " ";
    oss << "[" << levelStr[static_cast<int>(level_)] << "]";
    oss << "[" << pid_ << "]"
        << "[" << tid_ << "]";
    oss << "[" << file_ << ":" << func_ << ":" << line_ << "] ";
    oss << ss_.str() << "\n";

    std::lock_guard<std::mutex> lock(LogMutex());

    InitLogFile();
    auto &ofs = LogFile();

    if (!ofs.is_open()) {
        std::cout << oss.str();
        return;
    }

    ofs << oss.str();
    ofs.flush();

    RotateLogFile();
}
} // namespace virt::logger