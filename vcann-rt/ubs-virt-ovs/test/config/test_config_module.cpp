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
#include "test_config_module.h"

#include <filesystem>
#include <fstream>

#include "config_manager.h"
#include "config_module.h"

using namespace virt::ovs::config;

namespace ovs::ut {

void TestConfigModule::SetUp() {}
void TestConfigModule::TearDown()
{
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST_F(TestConfigModule, IsValidNumber)
{
    EXPECT_TRUE(IsValidNumber("123", false));
    EXPECT_FALSE(IsValidNumber("-123", false));
    EXPECT_TRUE(IsValidNumber("-123.45", true));
    EXPECT_FALSE(IsValidNumber("abc", false));
}

TEST_F(TestConfigModule, TrimConf)
{
    auto [sec, key, val] = TrimConf(" sec ", " key ", " val ");
    EXPECT_EQ(sec, "sec");
    EXPECT_EQ(key, "key");
    EXPECT_EQ(val, "val");
}

TEST_F(TestConfigModule, GetConf_Types)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["type_sec"]["uint_key"] = "42";
    mgr.configMap["type_sec"]["ulong_key"] = "10000000000";
    mgr.configMap["type_sec"]["float_key"] = "3.14";
    mgr.configMap["type_sec"]["str_key"] = "hello";
    mgr.configMap["type_sec"]["bool_key"] = "true";

    ConfigModule mod;
    uint32_t uval;
    EXPECT_EQ(mod.GetConf("type_sec", "uint_key", uval), ConfigCode::OK);
    EXPECT_EQ(uval, 42u);

    uint64_t ulval;
    EXPECT_EQ(mod.GetConf("type_sec", "ulong_key", ulval), ConfigCode::OK);
    EXPECT_EQ(ulval, 10000000000ULL);

    float fval;
    EXPECT_EQ(mod.GetConf("type_sec", "float_key", fval), ConfigCode::OK);
    EXPECT_FLOAT_EQ(fval, 3.14f);

    std::string sval;
    EXPECT_EQ(mod.GetConf("type_sec", "str_key", sval), ConfigCode::OK);
    EXPECT_EQ(sval, "hello");

    bool bval;
    EXPECT_EQ(mod.GetConf("type_sec", "bool_key", bval), ConfigCode::OK);
    EXPECT_TRUE(bval);
}

TEST_F(TestConfigModule, GetConf_InvalidType)
{
    ConfigModule mod;
    int unsupported;
    EXPECT_EQ(mod.GetConf("type_sec", "any", unsupported), ConfigCode::VALUE_TYPE_NOT_SUPPORTED);
}

TEST_F(TestConfigModule, GetBoolConf_False)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["bool_sec"]["flag"] = "false";

    ConfigModule mod;
    bool val = true;
    EXPECT_EQ(mod.GetConf("bool_sec", "flag", val), ConfigCode::OK);
    EXPECT_FALSE(val);
}

TEST_F(TestConfigModule, GetBoolConf_Invalid)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["bool_sec"]["flag"] = "not_a_bool";

    ConfigModule mod;
    bool val = false;
    EXPECT_EQ(mod.GetConf("bool_sec", "flag", val), ConfigCode::CONFIG_VALUE_INVALID);
}

TEST_F(TestConfigModule, GetUIntConf_InvalidNumber)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["bad"] = "not_a_number";

    ConfigModule mod;
    uint32_t val = 0;
    EXPECT_EQ(mod.GetConf("num_sec", "bad", val), ConfigCode::CONFIG_VALUE_INVALID);
}

TEST_F(TestConfigModule, GetFloatConf_InvalidNumber)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["bad_float"] = "xyz";

    ConfigModule mod;
    float val = 0.0f;
    EXPECT_EQ(mod.GetConf("num_sec", "bad_float", val), ConfigCode::CONFIG_VALUE_INVALID);
}

TEST_F(TestConfigModule, GetUIntConf_NegativeNumber)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["neg"] = "-5";

    ConfigModule mod;
    uint32_t val = 0;
    EXPECT_NE(mod.GetConf("num_sec", "neg", val), ConfigCode::OK);
}

TEST_F(TestConfigModule, IsValidNumber_MoreCases)
{
    EXPECT_TRUE(IsValidNumber("0", false));
    EXPECT_TRUE(IsValidNumber("1", false));
    EXPECT_FALSE(IsValidNumber("01", false));
    EXPECT_TRUE(IsValidNumber("3.0", true));
    EXPECT_TRUE(IsValidNumber(".5", true));
    EXPECT_TRUE(IsValidNumber("0.5", true));
    EXPECT_TRUE(IsValidNumber("-0.5", true));
}

TEST_F(TestConfigModule, GetUIntConf_Overflow)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["overflow32"] = "5000000000";

    ConfigModule mod;
    uint32_t val = 0;
    EXPECT_EQ(mod.GetConf("num_sec", "overflow32", val), ConfigCode::CONFIG_ARGUMENT_INVALID);
}

TEST_F(TestConfigModule, GetUIntConf_OutOfRange)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["huge"] = "999999999999999999999";

    ConfigModule mod;
    uint32_t val = 0;
    EXPECT_EQ(mod.GetConf("num_sec", "huge", val), ConfigCode::CONFIG_OUT_OF_RANGE);
}

TEST_F(TestConfigModule, GetULongConf_InvalidNumber)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["bad_ulong"] = "not_a_number";

    ConfigModule mod;
    uint64_t val = 0;
    EXPECT_EQ(mod.GetConf("num_sec", "bad_ulong", val), ConfigCode::CONFIG_VALUE_INVALID);
}

TEST_F(TestConfigModule, GetStringConf_SectionNotFound)
{
    ConfigModule mod;
    std::string val;
    EXPECT_EQ(mod.GetConf("nonexistent_sec_xyz", "any", val), ConfigCode::SECTION_NOT_EXIST);
}

TEST_F(TestConfigModule, GetStringConf_KeyNotFound)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["str_sec2"]["existing_key"] = "val";
    mgr.configMap["str_sec2"].erase("no_key");

    ConfigModule mod;
    std::string val;
    EXPECT_EQ(mod.GetConf("str_sec2", "no_key", val), ConfigCode::CONFIG_KEY_NOT_EXIST);
}

TEST_F(TestConfigModule, ConfigModule_Init)
{
    ConfigModule mod;
    auto ret = mod.Init("/nonexistent_config_dir");
    EXPECT_NE(ret, ConfigCode::OK);
}

TEST_F(TestConfigModule, GetFloatConf_OutOfRange)
{
    auto &mgr = ConfigManager::GetInstance();
    std::string hugeFloat(400, '9');
    hugeFloat += ".9";
    mgr.configMap["num_sec"]["big_float"] = hugeFloat;

    ConfigModule mod;
    float val = 0.0f;
    auto ret = mod.GetConf("num_sec", "big_float", val);
    EXPECT_NE(ret, ConfigCode::OK);
}

TEST_F(TestConfigModule, GetUIntConf_SuccessZero)
{
    auto &mgr = ConfigManager::GetInstance();
    mgr.configMap["num_sec"]["zero"] = "0";

    ConfigModule mod;
    uint32_t val = 99;
    EXPECT_EQ(mod.GetConf("num_sec", "zero", val), ConfigCode::OK);
    EXPECT_EQ(val, 0u);
}

TEST_F(TestConfigModule, ConfigModule_InitSuccess)
{
    std::filesystem::create_directory("test_cfg_mod_dir");
    std::ofstream out("test_cfg_mod_dir/test.conf");
    out << "[sec]\nkey=val\n";
    out.close();

    ConfigModule mod;
    auto ret = mod.Init("test_cfg_mod_dir");
    EXPECT_EQ(ret, ConfigCode::OK);

    std::string val;
    EXPECT_EQ(mod.GetConf("sec", "key", val), ConfigCode::OK);
    EXPECT_EQ(val, "val");

    std::filesystem::remove_all("test_cfg_mod_dir");
}

} // namespace ovs::ut
