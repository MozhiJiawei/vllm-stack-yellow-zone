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
#include "config_module.h"

#include <regex>

#include "config_manager.h"
#include "logger.h"

namespace virt::ovs::config {

ConfigCode ConfigModule::Init(const std::string &configDir)
{
    auto &confMgrRef = ConfigManager::GetInstance();
    const auto ret = confMgrRef.Init(configDir);
    if (ret != ConfigCode::OK) {
        return ret;
    }
    return ConfigCode::OK;
}

template <typename T>
ConfigCode GetNumConf(const std::string &section, const std::string &configKey, T &configVal)
{
    auto [trimSection, trimConfigKey, configValString] = TrimConf(section, configKey, "");
    auto ret = ConfigManager::GetInstance().GetConf(trimSection, trimConfigKey, configValString);
    if (ret != ConfigCode::OK) {
        LOG_WARN << "Unable to find section: " << trimSection << ", configKey: " << trimConfigKey
                 << ",ret is : " << static_cast<uint32_t>(ret);
        return ret;
    }

    if (!IsValidNumber(configValString, !std::is_unsigned_v<T>)) {
        LOG_WARN << "Config is invalid number: " << trimSection << ", configKey: " << trimConfigKey;
        return ConfigCode::CONFIG_VALUE_INVALID;
    }

    try {
        if constexpr (std::is_same<T, float>::value) {
            configVal = std::stof(configValString);
        } else if constexpr (std::is_same<T, uint32_t>::value) {
            configVal = static_cast<uint32_t>(std::stoul(configValString));
            if (std::to_string(configVal) != configValString) {
                throw std::invalid_argument("Invalid argument: Configuring non-integer types.");
            }
        } else if constexpr (std::is_same<T, uint64_t>::value) {
            configVal = static_cast<uint64_t>(std::stoull(configValString));
            if (std::to_string(configVal) != configValString) {
                throw std::invalid_argument("Invalid argument: Configuring non-integer types.");
            }
        }
    } catch (const std::invalid_argument &) {
        LOG_WARN << "Config is invalid argument: " << trimSection << ", configKey: " << trimConfigKey;
        return ConfigCode::CONFIG_ARGUMENT_INVALID;
    } catch (const std::out_of_range &) {
        LOG_WARN << "Config is out of range: " << trimSection << ", configKey: " << trimConfigKey;
        return ConfigCode::CONFIG_OUT_OF_RANGE;
    }

    return ConfigCode::OK;
}

ConfigCode ConfigModule::GetUIntConf(const std::string &section, const std::string &configKey, uint32_t &configVal)
{
    return GetNumConf(section, configKey, configVal);
}

ConfigCode ConfigModule::GetULongConf(const std::string &section, const std::string &configKey, uint64_t &configVal)
{
    return GetNumConf(section, configKey, configVal);
}

ConfigCode ConfigModule::GetFloatConf(const std::string &section, const std::string &configKey, float &configVal)
{
    return GetNumConf(section, configKey, configVal);
}

ConfigCode ConfigModule::GetStringConf(const std::string &section, const std::string &configKey, std::string &configVal)
{
    auto [trimSection, trimConfigKey, configValString] = TrimConf(section, configKey, "");
    ConfigCode ret = ConfigManager::GetInstance().GetConf(trimSection, trimConfigKey, configVal);
    if (ret != ConfigCode::OK) {
        LOG_WARN << "Unable to find section: " << trimSection << ", configKey: " << trimConfigKey
                 << ",ret is : " << static_cast<uint32_t>(ret);
        return ret;
    }

    return ConfigCode::OK;
}

ConfigCode ConfigModule::GetBoolConf(const std::string &section, const std::string &configKey, bool &configVal)
{
    auto [trimSection, trimConfigKey, configValString] = TrimConf(section, configKey, "");
    ConfigCode ret = ConfigManager::GetInstance().GetConf(trimSection, trimConfigKey, configValString);
    if (ret != ConfigCode::OK) {
        LOG_WARN << "Unable to find section: " << trimSection << ", configKey: " << trimConfigKey
                 << ",ret is : " << static_cast<uint32_t>(ret);
        return ret;
    }

    if (configValString == "true") {
        configVal = true;
    } else if (configValString == "false") {
        configVal = false;
    } else {
        LOG_WARN << "Config is invalid " << trimSection << ", configKey: " << trimConfigKey
                 << ",ret is : " << static_cast<uint32_t>(ret);
        return ConfigCode::CONFIG_VALUE_INVALID;
    }

    return ConfigCode::OK;
}

bool IsValidNumber(const std::string &str, bool allowFloating)
{
    try {
        if (allowFloating) {
            std::regex re(R"(^-?(0|[1-9]\d*)?(\.\d+)?$)");
            return std::regex_match(str, re);
        }

        std::regex re(R"(^[1-9]\d*$|^0$)");
        return std::regex_match(str, re);
    } catch (const std::regex_error &e) {
        return false;
    }
}
std::tuple<std::string, std::string, std::string> TrimConf(const std::string &section, const std::string &configKey,
                                                           const std::string &configVal)
{
    return {Trim(section), Trim(configKey), Trim(configVal)};
}
} // namespace virt::ovs::config