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

#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H
#include <cstdint>
#include <string>

#include "config_common_def.h"

namespace virt::ovs::config {
class ConfigModule {
public:
    static ConfigModule &GetInstance()
    {
        static ConfigModule instance;
        return instance;
    }
    ConfigCode Init(const std::string &configDir);
    /**
    * @brief read config file
    * @param [in] section: config session
    * @param [in] configKey: config key
    * @param [out] configVal: config value
    * @return ConfigCode, return ConfigCode::OK when success
    */
    template <typename T>
    ConfigCode GetConf(const std::string &section, const std::string &configKey, T &configVal);

private:
    ConfigCode GetUIntConf(const std::string &section, const std::string &configKey, uint32_t &configVal);

    ConfigCode GetULongConf(const std::string &section, const std::string &configKey, uint64_t &configVal);

    ConfigCode GetFloatConf(const std::string &section, const std::string &configKey, float &configVal);

    ConfigCode GetStringConf(const std::string &section, const std::string &configKey, std::string &configVal);

    ConfigCode GetBoolConf(const std::string &section, const std::string &configKey, bool &configVal);

    std::string configDefaultDir;
};

template <typename T>
ConfigCode ConfigModule::GetConf(const std::string &section, const std::string &configKey, T &configVal)
{
    if constexpr (std::is_same_v<T, uint32_t>) {
        return GetUIntConf(section, configKey, configVal);
    }

    if constexpr (std::is_same_v<T, uint64_t>) {
        return GetULongConf(section, configKey, configVal);
    }

    if constexpr (std::is_same_v<T, float>) {
        return GetFloatConf(section, configKey, configVal);
    }

    if constexpr (std::is_same_v<T, std::string>) {
        return GetStringConf(section, configKey, configVal);
    }

    if constexpr (std::is_same_v<T, bool>) {
        return GetBoolConf(section, configKey, configVal);
    }
    return ConfigCode::VALUE_TYPE_NOT_SUPPORTED;
}
std::tuple<std::string, std::string, std::string> TrimConf(const std::string &section, const std::string &configKey,
                                                           const std::string &configVal = "");
bool IsValidNumber(const std::string &str, bool allowFloating = false);

} // namespace virt::ovs::config

#endif // CONFIG_MODULE_H
