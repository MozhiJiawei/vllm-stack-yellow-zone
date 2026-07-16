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
#ifndef UBSVIRTOVS_CONFIG_MANAGER_H
#define UBSVIRTOVS_CONFIG_MANAGER_H
#include <cstdint>
#include <locale>
#include <map>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config_common_def.h"

namespace virt::ovs::config {

class Format {
public:
    const std::string charSectionStart;     // first character for session [
    const std::string charSectionEnd;       // last character for session [
    const std::string charAssign;           // equal sign
    const std::string charCommentSemicolon; // param comment (semicolon)
    const std::string charCommentHash;      // param comment (hash)
    explicit Format(std::string sectionStart = "[", std::string sectionEnd = "]", std::string assign = "=",
                    std::string commentSemicolon = ";", std::string commentHash = "#")
        : charSectionStart(std::move(sectionStart)),
          charSectionEnd(std::move(sectionEnd)),
          charAssign(std::move(assign)),
          charCommentSemicolon(std::move(commentSemicolon)),
          charCommentHash(std::move(commentHash))
    {
    }

    [[nodiscard]] inline bool IsSectionStart(const std::string &ch) const
    {
        return ch == charSectionStart;
    }

    [[nodiscard]] inline bool IsSectionEnd(const std::string &ch) const
    {
        return ch == charSectionEnd;
    }

    [[nodiscard]] inline bool IsAssign(const std::string &ch) const
    {
        return ch == charAssign;
    }

    [[nodiscard]] inline bool IsComment(const std::string &ch) const
    {
        return ch == charCommentSemicolon || ch == charCommentHash;
    }
};

using Section = std::map<std::string, std::string>;
using ConfigMap = std::map<std::string, Section>;

class ConfigManager {
public:
    static ConfigManager &GetInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    ConfigManager operator=(const ConfigManager &) = delete;

    ConfigManager(const ConfigManager &) = delete;

    ConfigCode Init(const std::string &confDir, const std::string &filePrefix = "");

    /* *
    * @brief read single config
    * @param[in] section: config section
    * @param[in] configKey: config param key
    * @param[out] configVal: config param value
    * @return ConfigCode, success return ConfigCode::OK
    */
    ConfigCode GetConf(const std::string &section, const std::string &configKey, std::string &configVal);

private:
    ConfigManager() {}

    ConfigCode ParseFile(const std::string &filePath); // parse file and check, store in memory

    void ParseLine(const std::string &filePath, std::string line, const size_t &lineCount, std::string &tempSection);
    void ParseSection(const std::string &filePath, const std::string &line, const size_t &lineCount,
                      std::string &tempSection);
    void ParseConf(const std::string &filePath, const std::string &line, const size_t &lineCount,
                   std::string &tempSection);

    ConfigCode ReadConfFile(const std::string &filePath); // read config file to memory

    Format format;
    std::unordered_map<std::string, std::vector<std::string>> parseErrors; // parse error message
    std::shared_mutex rwLock;
    ConfigMap configMap; // store all config
    std::unordered_set<std::string> fileSet;
};

std::string Trim(const std::string &str, const std::locale &loc = std::locale{"C"});
std::string CatString(const std::vector<std::string> &infoVec, const std::string &delimiter);
std::string FormatErrorMessage(const std::string &message, size_t lineCount, const std::string &section = "",
                               const std::string &configKey = "", const std::string &configVal = "");
ConfigCode CheckParamValidation(const std::string &section, const std::string &configKey, const std::string &configVal,
                                bool checkValue = false);

bool IsConfFile(const std::string &filename); // judge is conf file
ConfigCode TravelDepthLimitedFiles(std::vector<std::string> &filePaths, const std::string &path, int depth);
bool CheckNoIllegalChars(const std::string &str, bool isConfigVal = false);
std::string PathJoin(const std::string &baseDir, const std::string &baseName); // get confile path
} // namespace virt::ovs::config

#endif // UBSVIRTOVS_CONFIG_MANAGER_H
