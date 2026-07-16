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
#include "config_manager.h"

#include <dirent.h>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>

#include "config_common_def.h"
#include "logger.h"

namespace virt::ovs::config {

ConfigCode ConfigManager::Init(const std::string &confDir, const std::string &filePrefix)
{
    std::vector<std::string> filePaths;
    auto ret = TravelDepthLimitedFiles(filePaths, confDir, 0);
    if (ret != ConfigCode::OK) {
        return ret;
    }

    std::unique_lock<std::shared_mutex> guard(rwLock);
    for (const auto &filePath : filePaths) {
        // file is loaded
        if (fileSet.count(filePath)) {
            LOG_ERROR << "Warning: File: " << filePath << "has already been loaded.";
            continue;
        }
        // file prefix is not null and file name not match
        if (!filePrefix.empty() && filePath.substr(filePath.rfind('/') + 1).find(filePrefix) != 0) {
            continue;
        }
        ret = ParseFile(filePath);
        // parse file failed
        if (ret != ConfigCode::OK) {
            LOG_ERROR << "Warning: Unable to parse file: " << filePath << ".";
        } else {
            fileSet.emplace(filePath);
        }
    }
    if (!parseErrors.empty()) {
        LOG_ERROR << "Unable to parse the configuration.";
        for (const auto &pair : parseErrors) {
            LOG_ERROR << "fileName: " << pair.first << ",warnings:\n" << CatString(pair.second, "\n");
        }
    }
    parseErrors.clear();
    return ret;
}

ConfigCode TravelDepthLimitedFiles(std::vector<std::string> &filePaths, const std::string &path, int depth)
{
    if (depth > CONFIG_DIR_MAX_DEPTH) {
        return ConfigCode::CONFIG_FOLDER_MAX_DEPTH;
    }

    DIR *pd = opendir(path.c_str());
    if (pd == nullptr) {
        LOG_WARN << "Unable to open dir " << path;
        return ConfigCode::CONFIG_FOLDER_OPEN_ERROR;
    }
    const dirent *dir;
    struct stat statBuf {
    };
    while ((dir = readdir(pd)) != nullptr) {
        std::string dName = dir->d_name;
        if (dName == "." || dName == "..") {
            continue;
        }
        std::string subFile = PathJoin(path, dName);
        if (lstat(subFile.c_str(), &statBuf)) {
            LOG_WARN << "Unable to get file status " << subFile << ".";
            continue;
        }

        if (S_ISDIR(statBuf.st_mode)) {
            ConfigCode ret = TravelDepthLimitedFiles(filePaths, subFile, depth + 1);
            if (ret != ConfigCode::OK) {
                closedir(pd);
                return ret;
            }
        } else if (S_ISREG(statBuf.st_mode) && IsConfFile(dName)) {
            filePaths.emplace_back(subFile);
        }
    }
    closedir(pd);
    return ConfigCode::OK;
}

ConfigCode ConfigManager::ParseFile(const std::string &filePath)
{
    std::error_code ec;
    const std::filesystem::path canonicalPath = std::filesystem::canonical(filePath, ec);
    if (ec) {
        std::cerr << "Warning: Could not canonicalize file path " << filePath << " ,err: " << ec.message() << std::endl;
        return ConfigCode::CONFIG_FILE_READ_ERROR;
    }
    return ReadConfFile(canonicalPath.string());
}

ConfigCode ConfigManager::ReadConfFile(const std::string &filePath)
{
    std::ifstream fileStream(filePath);
    // failed to open file
    if (!fileStream.is_open()) {
        std::cerr << "Warning: Can not open file: " << filePath << " to read." << std::endl;
        return ConfigCode::CONFIG_FILE_READ_ERROR;
    }
    std::string defaultSection = filePath.substr(filePath.find_last_of("/\\") + 1,
                                                 filePath.size() - filePath.find_last_of("/\\") - 1 - SUFFIX_SIZE);
    std::string tempSection = Trim(defaultSection);

    // read by line
    std::string line; // store current line
    size_t lineCount = 1;
    while (std::getline(fileStream, line) && (lineCount <= (CONFIG_MAX_LINES + 1))) {
        ParseLine(filePath, line, lineCount++, tempSection);
    }
    fileStream.close();
    return ConfigCode::OK;
}

void ConfigManager::ParseLine(const std::string &filePath, std::string line, const size_t &lineCount,
                              std::string &tempSection)
{
    if (lineCount > CONFIG_MAX_LINES) {
        parseErrors[filePath].emplace_back("Warning: Maximum line count exceeded.");
        return;
    }

    line = Trim(line);
    size_t equalPos = line.find('=');
    // ignore annotation
    if (line.empty() || format.IsComment(std::string(1, line.front()))) {
        return;
    }
    if (format.IsSectionStart(std::string(1, line.front())) && format.IsSectionEnd(std::string(1, line.back()))) {
        ParseSection(filePath, line, lineCount, tempSection);
    } else if (equalPos != std::string::npos && equalPos > 0 && equalPos < line.size() - 1) {
        ParseConf(filePath, line, lineCount, tempSection);
    } else {
        parseErrors[filePath].emplace_back("Warning: Invalid line content. Line: " + std::to_string(lineCount) + ".");
    }
}

void ConfigManager::ParseSection(const std::string &filePath, const std::string &line, const size_t &lineCount,
                                 std::string &tempSection)
{
    const std::regex sectionChars(R"(\[\s*(.*?)\s*\])");
    std::string section = std::regex_replace(line, sectionChars, R"($1)");
    // length illegal
    if (section.size() < CONFIG_MIN_FIELD_LENGTH || section.size() > CONFIG_SECTION_MAX_FIELD_LENGTH) {
        std::string message = "Warning: The length of section is out of range( " +
                              std::to_string(CONFIG_MIN_FIELD_LENGTH) + " to " +
                              std::to_string(CONFIG_SECTION_MAX_FIELD_LENGTH) + ").";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, section));
        return;
    }
    // illegal char

    if (!CheckNoIllegalChars(section)) {
        std::string message = "Warning: Section has illegal character.";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, section));
        return;
    }
    tempSection = section;
    // add key to map when section appears for the first time
    if (configMap.find(tempSection) == configMap.end()) {
        configMap[tempSection];
    }
}

void ConfigManager::ParseConf(const std::string &filePath, const std::string &line, const size_t &lineCount,
                              std::string &tempSection)
{
    size_t pos = line.find('=');
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    key = Trim(key);
    value = Trim(value);
    // illegal length
    if (key.size() > CONFIG_KEY_MAX_FIELD_LENGTH || key.size() < CONFIG_MIN_FIELD_LENGTH ||
        value.size() > CONFIG_VALUE_MAX_FIELD_LENGTH) {
        std::string message =
            "Warning: The length of key is out of range(key: " + std::to_string(CONFIG_MIN_FIELD_LENGTH) + " to " +
            std::to_string(CONFIG_KEY_MAX_FIELD_LENGTH) + " ,value:" + std::to_string(CONFIG_MIN_FIELD_LENGTH) +
            " to " + std::to_string(CONFIG_VALUE_MAX_FIELD_LENGTH) + ").";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, "", key, value));
        return;
    }
    // illegal chars
    if (!CheckNoIllegalChars(key)) {
        std::string message = "Warning: Section's key has illegal character.";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, tempSection, key));
        return;
    }
    if (!CheckNoIllegalChars(value, true)) {
        std::string message = "Warning: The configuration value contains illegal chars.";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, tempSection, key, value));
        return;
    }
    // duplicate configuration item
    if (configMap[tempSection].find(key) != configMap[tempSection].end()) {
        std::string message = "Warning: Duplicate key in section.";
        parseErrors[filePath].emplace_back(FormatErrorMessage(message, lineCount, tempSection, key));
        return;
    }
    configMap[tempSection][key] = value;
}

ConfigCode ConfigManager::GetConf(const std::string &section, const std::string &configKey, std::string &configVal)
{
    ConfigCode ret = CheckParamValidation(section, configKey, configVal, false);
    if (ret != ConfigCode::OK) {
        return ret;
    }

    // section not exist
    if (configMap.find(section) == configMap.end()) {
        LOG_WARN << "Unable to find section: " << section;
        return ConfigCode::SECTION_NOT_EXIST;
    }

    // key not exist in section
    if (configMap[section].find(configKey) == configMap[section].end()) {
        LOG_WARN << "Unable to find key: " << configKey << " in section: " << section;
        return ConfigCode::CONFIG_KEY_NOT_EXIST;
    }
    configVal = configMap[section][configKey];
    return ConfigCode::OK;
}

std::string FormatErrorMessage(const std::string &message, size_t lineCount, const std::string &section,
                               const std::string &configKey, const std::string &configVal)
{
    std::ostringstream oss;
    oss << message << " Line: " << std::to_string(lineCount) << ".";
    if (!section.empty()) {
        oss << " Section: " << section << ".";
    }
    if (!configKey.empty()) {
        oss << " Key: " << configKey << ".";
    }
    if (!configVal.empty()) {
        oss << " Value: " << configVal << ".";
    }
    return oss.str();
}

std::string CatString(const std::vector<std::string> &infoVec, const std::string &delimiter)
{
    if (infoVec.empty()) {
        return "";
    }
    std::string outputStr;
    auto it = infoVec.begin();
    outputStr += *it;
    it++;
    for (; it != infoVec.end(); it++) {
        outputStr += delimiter + (*it);
    }
    return std::move(outputStr);
}

bool CheckNoIllegalChars(const std::string &str, bool isConfigVal)
{
    const std::regex nonValChars(R"(^[a-zA-Z0-9._-]+$)");
    const std::regex valChars(R"(^[a-zA-Z0-9._:,/;\-]+$)");
    const std::regex &legalChars = isConfigVal ? valChars : nonValChars;

    if (str.empty()) {
        return true;
    }
    return std::regex_match(str, legalChars);
}

std::string Trim(const std::string &str, const std::locale &loc)
{
    std::string s = str;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&loc](char ch) { return !std::isspace(ch, loc); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&loc](char ch) { return !std::isspace(ch, loc); }).base(), s.end());
    return s;
}
bool IsConfFile(const std::string &filename)
{
    return filename.size() > SUFFIX_SIZE && filename.compare(filename.size() - SUFFIX_SIZE, SUFFIX_SIZE, ".conf") == 0;
}
std::string PathJoin(const std::string &baseDir, const std::string &baseName)
{
    if (baseDir.empty()) {
        return baseName;
    }

    const char delimiter = '/';
    std::string result = baseDir;
    if (result.back() != delimiter) {
        result.append(1, delimiter);
    }
    result += baseName;
    return result;
}
ConfigCode CheckParamValidation(const std::string &section, const std::string &configKey, const std::string &configVal,
                                bool checkValue)
{
    // length check
    if (section.size() > CONFIG_SECTION_MAX_FIELD_LENGTH || section.size() < CONFIG_MIN_FIELD_LENGTH) {
        LOG_WARN << "Section length invalid";
        return ConfigCode::SECTION_LENGTH_INVALID;
    }
    if (configKey.size() > CONFIG_KEY_MAX_FIELD_LENGTH || configKey.size() < CONFIG_MIN_FIELD_LENGTH) {
        LOG_WARN << "Key length invalid";
        return ConfigCode::KEY_LENGTH_INVALID;
    }
    if ((configVal.empty() || configVal.size() > CONFIG_VALUE_MAX_FIELD_LENGTH) && checkValue) {
        LOG_WARN << "Value length too long or too short";
        return ConfigCode::VALUE_LENGTH_INVALID;
    }
    return ConfigCode::OK;
}

} // namespace virt::ovs::config