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
#include "test_config_manager.h"

#include "config_manager.h"

#include <filesystem>
#include <fstream>

using namespace virt::ovs::config;

namespace ovs::ut {

void TestConfigManager::SetUp() {}
void TestConfigManager::TearDown()
{
    GlobalMockObject::verify();
    GlobalMockObject::reset();
}

TEST_F(TestConfigManager, Trim_Basic)
{
    EXPECT_EQ(Trim("  hello  "), "hello");
    EXPECT_EQ(Trim(""), "");
    EXPECT_EQ(Trim(" \t\n "), "");
}

TEST_F(TestConfigManager, CatString_Basic)
{
    std::vector<std::string> vec = {"a", "b", "c"};
    EXPECT_EQ(CatString(vec, ","), "a,b,c");
    EXPECT_EQ(CatString({}, ","), "");
}

TEST_F(TestConfigManager, CheckNoIllegalChars_Basic)
{
    EXPECT_TRUE(CheckNoIllegalChars("valid_key-1"));
    EXPECT_FALSE(CheckNoIllegalChars("invalid key"));
    EXPECT_TRUE(CheckNoIllegalChars("192.168.1.1", true));
}

TEST_F(TestConfigManager, PathJoin_Basic)
{
    EXPECT_EQ(PathJoin("/tmp", "file.conf"), "/tmp/file.conf");
    EXPECT_EQ(PathJoin("/tmp/", "file.conf"), "/tmp/file.conf");
    EXPECT_EQ(PathJoin("", "file.conf"), "file.conf");
}

TEST_F(TestConfigManager, IsConfFile_Basic)
{
    EXPECT_TRUE(IsConfFile("test.conf"));
    EXPECT_FALSE(IsConfFile("test.txt"));
}

TEST_F(TestConfigManager, Format_Checks)
{
    Format fmt;
    EXPECT_TRUE(fmt.IsSectionStart("["));
    EXPECT_TRUE(fmt.IsSectionEnd("]"));
    EXPECT_TRUE(fmt.IsAssign("="));
    EXPECT_TRUE(fmt.IsComment(";"));
    EXPECT_TRUE(fmt.IsComment("#"));
}

TEST_F(TestConfigManager, ParseConf_Valid)
{
    ConfigManager mgr;
    std::string tempSection = "section1";
    mgr.ParseConf("file.conf", "key1=value1", 1, tempSection);

    std::string val;
    EXPECT_EQ(mgr.GetConf("section1", "key1", val), ConfigCode::OK);
    EXPECT_EQ(val, "value1");
}

TEST_F(TestConfigManager, ParseSection_Valid)
{
    ConfigManager mgr;
    std::string tempSection = "";
    mgr.ParseSection("file.conf", "[section2]", 1, tempSection);
    EXPECT_EQ(tempSection, "section2");
    EXPECT_TRUE(mgr.configMap.find("section2") != mgr.configMap.end());
}

TEST_F(TestConfigManager, GetConf_SectionNotExist)
{
    ConfigManager mgr;
    std::string val;
    EXPECT_EQ(mgr.GetConf("not_exist", "key", val), ConfigCode::SECTION_NOT_EXIST);
}

TEST_F(TestConfigManager, GetConf_KeyNotExist)
{
    ConfigManager mgr;
    mgr.configMap["sec1"] = {};
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec1", "not_exist", val), ConfigCode::CONFIG_KEY_NOT_EXIST);
}

TEST_F(TestConfigManager, ReadConfFile_Success)
{
    std::string testFile = "test_read.conf";
    std::ofstream out(testFile);
    out << "[sec1]\nkey1=val1\n# comment\nkey2=val2\n";
    out.close();

    ConfigManager mgr;
    EXPECT_EQ(mgr.ReadConfFile(testFile), ConfigCode::OK);

    std::string val;
    EXPECT_EQ(mgr.GetConf("sec1", "key1", val), ConfigCode::OK);
    EXPECT_EQ(val, "val1");
    EXPECT_EQ(mgr.GetConf("sec1", "key2", val), ConfigCode::OK);
    EXPECT_EQ(val, "val2");

    std::filesystem::remove(testFile);
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_Success)
{
    std::filesystem::create_directory("test_conf_dir");
    std::ofstream out("test_conf_dir/test.conf");
    out << "[sec1]\nkey=val\n";
    out.close();

    std::vector<std::string> paths;
    EXPECT_EQ(TravelDepthLimitedFiles(paths, "test_conf_dir", 0), ConfigCode::OK);
    EXPECT_EQ(paths.size(), 1u);

    std::filesystem::remove_all("test_conf_dir");
}

TEST_F(TestConfigManager, ParseFile_CanonicalFail)
{
    ConfigManager mgr;
    EXPECT_EQ(mgr.ParseFile("/nonexistent/path/file.conf"), ConfigCode::CONFIG_FILE_READ_ERROR);
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_DepthExceeded)
{
    std::vector<std::string> paths;
    EXPECT_EQ(TravelDepthLimitedFiles(paths, ".", CONFIG_DIR_MAX_DEPTH + 1), ConfigCode::CONFIG_FOLDER_MAX_DEPTH);
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_OpenDirFail)
{
    std::vector<std::string> paths;
    EXPECT_EQ(TravelDepthLimitedFiles(paths, "/nonexistent_dir_xyz", 0), ConfigCode::CONFIG_FOLDER_OPEN_ERROR);
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_SubDirRecurse)
{
    std::filesystem::create_directories("test_conf_dir/sub");
    std::ofstream("test_conf_dir/test.conf").close();
    std::ofstream("test_conf_dir/sub/nested.conf").close();
    std::ofstream("test_conf_dir/sub/notconf.txt").close();

    std::vector<std::string> paths;
    EXPECT_EQ(TravelDepthLimitedFiles(paths, "test_conf_dir", 0), ConfigCode::OK);
    EXPECT_EQ(paths.size(), 2u);

    std::filesystem::remove_all("test_conf_dir");
}

TEST_F(TestConfigManager, ParseSection_TooShort)
{
    ConfigManager mgr;
    std::string tempSection = "old";
    mgr.ParseSection("file.conf", "[]", 1, tempSection);
    EXPECT_EQ(tempSection, "old");
}

TEST_F(TestConfigManager, ParseSection_IllegalChars)
{
    ConfigManager mgr;
    std::string tempSection = "old";
    mgr.ParseSection("file.conf", "[sec tion]", 1, tempSection);
    EXPECT_EQ(tempSection, "old");
}

TEST_F(TestConfigManager, ParseConf_KeyTooLong)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    std::string longKey(CONFIG_KEY_MAX_FIELD_LENGTH + 1, 'k');
    std::string line = longKey + "=val";
    mgr.ParseConf("file.conf", line, 1, tempSection);
    std::string val;
    EXPECT_NE(mgr.GetConf("sec", longKey, val), ConfigCode::OK);
}
TEST_F(TestConfigManager, ParseConf_ValueTooLong)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    std::string longVal(CONFIG_VALUE_MAX_FIELD_LENGTH + 1, 'v');
    std::string line = "key=" + longVal;
    mgr.ParseConf("file.conf", line, 1, tempSection);
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec", "key", val), ConfigCode::CONFIG_KEY_NOT_EXIST);
}

TEST_F(TestConfigManager, ParseConf_KeyIllegalChars)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseConf("file.conf", "key name=val", 1, tempSection);
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec", "key name", val), ConfigCode::CONFIG_KEY_NOT_EXIST);
}

TEST_F(TestConfigManager, ParseConf_ValueIllegalChars)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseConf("file.conf", "key=val ue", 1, tempSection);
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec", "key", val), ConfigCode::CONFIG_KEY_NOT_EXIST);
}

TEST_F(TestConfigManager, ParseConf_DuplicateKey)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.ParseConf("file.conf", "key=val1", 1, tempSection);
    mgr.ParseConf("file.conf", "key=val2", 1, tempSection);
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec", "key", val), ConfigCode::OK);
    EXPECT_EQ(val, "val1");
}

TEST_F(TestConfigManager, ParseLine_InvalidContent)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseLine("file.conf", "invalid_line_without_equal", 1, tempSection);
}

TEST_F(TestConfigManager, ReadConfFile_MaxLineExceeded)
{
    std::string testFile = "test_maxline.conf";
    std::ofstream out(testFile);
    out << "[sec]\n";
    for (size_t i = 0; i < CONFIG_MAX_LINES; ++i) {
        out << "k" << i << "=v" << i << "\n";
    }
    out.close();

    ConfigManager mgr;
    EXPECT_EQ(mgr.ReadConfFile(testFile), ConfigCode::OK);
    std::filesystem::remove(testFile);
}

TEST_F(TestConfigManager, CheckParamValidation_SectionInvalid)
{
    std::string dummy;
    EXPECT_EQ(CheckParamValidation("", "key", dummy), ConfigCode::SECTION_LENGTH_INVALID);
    std::string longSec(CONFIG_SECTION_MAX_FIELD_LENGTH + 1, 's');
    EXPECT_EQ(CheckParamValidation(longSec, "key", dummy), ConfigCode::SECTION_LENGTH_INVALID);
}

TEST_F(TestConfigManager, CheckParamValidation_KeyInvalid)
{
    std::string dummy;
    EXPECT_EQ(CheckParamValidation("sec", "", dummy), ConfigCode::KEY_LENGTH_INVALID);
    std::string longKey(CONFIG_KEY_MAX_FIELD_LENGTH + 1, 'k');
    EXPECT_EQ(CheckParamValidation("sec", longKey, dummy), ConfigCode::KEY_LENGTH_INVALID);
}

TEST_F(TestConfigManager, CheckParamValidation_ValueInvalid)
{
    std::string emptyVal;
    EXPECT_EQ(CheckParamValidation("sec", "key", emptyVal, true), ConfigCode::VALUE_LENGTH_INVALID);
    std::string longVal(CONFIG_VALUE_MAX_FIELD_LENGTH + 1, 'v');
    EXPECT_EQ(CheckParamValidation("sec", "key", longVal, true), ConfigCode::VALUE_LENGTH_INVALID);
}

TEST_F(TestConfigManager, CheckParamValidation_OK)
{
    std::string val = "valid";
    EXPECT_EQ(CheckParamValidation("sec", "key", val, true), ConfigCode::OK);
    EXPECT_EQ(CheckParamValidation("sec", "key", val), ConfigCode::OK);
}

TEST_F(TestConfigManager, CheckNoIllegalChars_ValueMode)
{
    EXPECT_TRUE(CheckNoIllegalChars("valid_val:1/2,3;4-5", true));
    EXPECT_FALSE(CheckNoIllegalChars("invalid value!", true));
    EXPECT_TRUE(CheckNoIllegalChars("", true));
}

TEST_F(TestConfigManager, FormatErrorMessage_AllFields)
{
    std::string msg = FormatErrorMessage("Test error", 42, "section1", "key1", "value1");
    EXPECT_NE(msg.find("Test error"), std::string::npos);
    EXPECT_NE(msg.find("42"), std::string::npos);
    EXPECT_NE(msg.find("section1"), std::string::npos);
    EXPECT_NE(msg.find("key1"), std::string::npos);
    EXPECT_NE(msg.find("value1"), std::string::npos);
}

TEST_F(TestConfigManager, Init_TravelDepthFailed)
{
    ConfigManager mgr;
    auto ret = mgr.Init("/nonexistent_dir_xyz");
    EXPECT_EQ(ret, ConfigCode::CONFIG_FOLDER_OPEN_ERROR);
}

TEST_F(TestConfigManager, Init_PrefixNotMatch)
{
    std::filesystem::create_directory("test_prefix_dir");
    std::ofstream("test_prefix_dir/app.conf").close();
    std::ofstream("test_prefix_dir/not_match.txt").close();

    ConfigManager mgr;
    auto ret = mgr.Init("test_prefix_dir", "nomatch");
    EXPECT_EQ(ret, ConfigCode::OK);

    std::filesystem::remove_all("test_prefix_dir");
}

TEST_F(TestConfigManager, Init_WithPrefixFilter)
{
    std::filesystem::create_directory("test_filter_dir");
    std::ofstream out("test_filter_dir/ubs.conf");
    out << "[sec]\nkey=val\n";
    out.close();
    std::ofstream("test_filter_dir/other.conf").close();

    ConfigManager mgr;
    auto ret = mgr.Init("test_filter_dir", "ubs");
    EXPECT_EQ(ret, ConfigCode::OK);
    std::string val;
    EXPECT_EQ(mgr.GetConf("sec", "key", val), ConfigCode::OK);
    EXPECT_EQ(val, "val");

    std::filesystem::remove_all("test_filter_dir");
}

TEST_F(TestConfigManager, Init_FileAlreadyLoaded)
{
    std::filesystem::create_directory("test_reload_dir");
    std::ofstream out("test_reload_dir/test.conf");
    out << "[sec]\nkey=val\n";
    out.close();

    ConfigManager mgr;
    EXPECT_EQ(mgr.Init("test_reload_dir"), ConfigCode::OK);
    EXPECT_EQ(mgr.Init("test_reload_dir"), ConfigCode::OK);

    std::filesystem::remove_all("test_reload_dir");
}

TEST_F(TestConfigManager, ReadConfFile_FileOpenFailed)
{
    ConfigManager mgr;
    EXPECT_EQ(mgr.ReadConfFile("/nonexistent_dir/test.conf"), ConfigCode::CONFIG_FILE_READ_ERROR);
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_LstatFail)
{
    std::filesystem::create_directory("test_lstat_dir");
    std::ofstream("test_lstat_dir/good.conf").close();

    std::vector<std::string> paths;
    MOCKER(lstat).stubs().will(returnValue(-1));
    auto ret = TravelDepthLimitedFiles(paths, "test_lstat_dir", 0);
    EXPECT_EQ(ret, ConfigCode::OK);
    MOCKER(lstat).reset();

    std::filesystem::remove_all("test_lstat_dir");
}

TEST_F(TestConfigManager, ParseLine_CommentSemicolon)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseLine("file.conf", "; this is a comment", 1, tempSection);
    SUCCEED();
}

TEST_F(TestConfigManager, ParseLine_CommentHash)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseLine("file.conf", "# this is a comment", 1, tempSection);
    SUCCEED();
}

TEST_F(TestConfigManager, ParseLine_EmptyLine)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseLine("file.conf", "", 1, tempSection);
    SUCCEED();
}

TEST_F(TestConfigManager, ParseLine_WhitespaceOnly)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseLine("file.conf", "   ", 1, tempSection);
    SUCCEED();
}

TEST_F(TestConfigManager, ParseSection_ExistingSection)
{
    ConfigManager mgr;
    std::string tempSection = "old";
    mgr.configMap["existing"];
    mgr.ParseSection("file.conf", "[existing]", 1, tempSection);
    EXPECT_EQ(tempSection, "existing");
}

TEST_F(TestConfigManager, CatString_SingleElement)
{
    std::vector<std::string> vec = {"single"};
    EXPECT_EQ(CatString(vec, ","), "single");
}

TEST_F(TestConfigManager, ParseLine_MaxLineExceeded)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.ParseLine("file.conf", "key=val", CONFIG_MAX_LINES + 2, tempSection);
    EXPECT_FALSE(mgr.parseErrors.empty());
}

TEST_F(TestConfigManager, CheckNoIllegalChars_NonValChars)
{
    EXPECT_FALSE(CheckNoIllegalChars("key with space"));
    EXPECT_FALSE(CheckNoIllegalChars("key@invalid"));
}

TEST_F(TestConfigManager, Init_ParseErrorStored)
{
    std::filesystem::create_directory("test_parse_err_dir");
    std::ofstream out("test_parse_err_dir/bad.conf");
    out << "[ok_section]\n=no_key\n";
    out.close();

    ConfigManager mgr;
    mgr.Init("test_parse_err_dir");

    std::filesystem::remove_all("test_parse_err_dir");
}

TEST_F(TestConfigManager, ParseConf_KeyTooShort)
{
    ConfigManager mgr;
    std::string tempSection = "sec";
    mgr.configMap["sec"];
    mgr.ParseConf("file.conf", "  =val", 1, tempSection);
    std::string val;
    EXPECT_NE(mgr.GetConf("sec", "", val), ConfigCode::OK);
}

TEST_F(TestConfigManager, ParseConf_SectionAutoCreate)
{
    ConfigManager mgr;
    std::string tempSection = "auto_sec";
    mgr.ParseConf("file.conf", "key=val", 1, tempSection);
    std::string val;
    EXPECT_EQ(mgr.GetConf("auto_sec", "key", val), ConfigCode::OK);
    EXPECT_EQ(val, "val");
}

TEST_F(TestConfigManager, Format_CustomDelimiters)
{
    Format fmt("((", "))", ":", "//", "--");
    EXPECT_TRUE(fmt.IsSectionStart("(("));
    EXPECT_TRUE(fmt.IsSectionEnd("))"));
    EXPECT_TRUE(fmt.IsAssign(":"));
    EXPECT_TRUE(fmt.IsComment("//"));
    EXPECT_TRUE(fmt.IsComment("--"));
}

TEST_F(TestConfigManager, TravelDepthLimitedFiles_DirWithInvalidEntry)
{
    std::filesystem::create_directory("test_mixed_dir");
    std::ofstream("test_mixed_dir/valid.conf").close();
    std::ofstream("test_mixed_dir/notconf.txt").close();

    std::vector<std::string> paths;
    auto ret = TravelDepthLimitedFiles(paths, "test_mixed_dir", 0);
    EXPECT_EQ(ret, ConfigCode::OK);
    EXPECT_EQ(paths.size(), 1u);

    std::filesystem::remove_all("test_mixed_dir");
}

TEST_F(TestConfigManager, GetConf_FromMultipleSections)
{
    ConfigManager mgr;
    std::string sec1 = "s1";
    std::string sec2 = "s2";
    mgr.configMap[sec1]["k"] = "v1";
    mgr.configMap[sec2]["k"] = "v2";

    std::string val;
    EXPECT_EQ(mgr.GetConf("s1", "k", val), ConfigCode::OK);
    EXPECT_EQ(val, "v1");
    EXPECT_EQ(mgr.GetConf("s2", "k", val), ConfigCode::OK);
    EXPECT_EQ(val, "v2");
}

} // namespace ovs::ut
