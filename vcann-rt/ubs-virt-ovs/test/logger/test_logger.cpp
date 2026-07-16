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

#include "test_logger.h"
#include "logger.h"

#include <dirent.h>
#include <securec.h>

using namespace virt::logger;
using mockcpp::atLeast;
namespace ovs::ut {

TEST_F(TestLogger, NowFilename_ReturnNonEmptyString)
{
    std::string filename = NowFilename();
    EXPECT_FALSE(filename.empty());
    EXPECT_GT(filename.size(), 0u);
    EXPECT_EQ(filename.size(), 15);
}

TEST_F(TestLogger, LoggerEntry_SubmitDoesNotThrow)
{
    Logger entry(LoggerLevel::INFO, "logger_test.cpp", "LoggerEntry_SubmitDoesNotThrow", 123);
    EXPECT_NO_THROW({
        entry << "This is a test log";
        entry.Submit();
    });
}

TEST_F(TestLogger, EnsureLogDir_MkdirFailed)
{
    MOCKER(mkdir).stubs().will(returnValue(-1));
    MOCKER(stat).stubs().will(returnValue(-1));
    EnsureLogDir();
    MOCKER(stat).reset();
    MOCKER(mkdir).reset();
}

TEST_F(TestLogger, EnsureLogDir_DirAlreadyExists)
{
    MOCKER(stat).stubs().will(returnValue(0));
    MOCKER(mkdir).expects(never());
    EnsureLogDir();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(stat).reset();
    MOCKER(mkdir).reset();
}

TEST_F(TestLogger, EnsureLogDir_MkdirSuccess)
{
    MOCKER(stat).stubs().will(returnValue(-1));
    MOCKER(mkdir).stubs().will(returnValue(0));
    EnsureLogDir();
    MOCKER(stat).reset();
    MOCKER(mkdir).reset();
}

TEST_F(TestLogger, GetFileSize_Failed)
{
    MOCKER(stat).stubs().will(returnValue(-1));
    EXPECT_EQ(GetFileSize("not_exist_file"), 0u);
    MOCKER(stat).reset();
}

TEST_F(TestLogger, GetFileSize_Success)
{
    MOCKER(stat).stubs().will(returnValue(0));
    EXPECT_EQ(GetFileSize("test.log"), 0u);
    MOCKER(stat).reset();
}

TEST_F(TestLogger, NowFilename_StrftimeFailed)
{
    MOCKER(strftime).stubs().will(returnValue(size_t(0)));
    EXPECT_EQ(NowFilename(), "00000000_000000");
    MOCKER(strftime).reset();
}

static struct dirent *MakeDirent(const char *name)
{
    static struct dirent ent;
    memset_s(&ent, sizeof(ent), 0, sizeof(ent));
    strncpy_s(ent.d_name, sizeof(ent.d_name), name, sizeof(ent.d_name) - 1);
    return &ent;
}

TEST_F(TestLogger, CleanupOldRotateLogFile_OpendirFail)
{
    MOCKER(opendir).expects(once()).will(returnValue((DIR *)nullptr));
    MOCKER(readdir).expects(never());
    MOCKER(closedir).expects(never());
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_NoDelete)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("virt_ovs_20240101010101.tar.gz")))
        .then(returnValue(MakeDirent("virt_ovs_20240102010101.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(1));
    MOCKER(strftime).stubs().will(returnValue(static_cast<size_t>(1)));

    MOCKER(mktime)
        .stubs()
        .will(returnValue(time_t(1)))
        .then(returnValue(time_t(2)))
        .then(returnValue(time_t(3)))
        .then(returnValue(time_t(4)));

    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(strftime).reset();
    MOCKER(mktime).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_IgnoreInvalid)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("randon.txt")))
        .then(returnValue(MakeDirent("virt_ovs_invalid.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(1));
    MOCKER(strftime).stubs().will(returnValue(static_cast<size_t>(10)));

    MOCKER(unlink).expects(never()).will(returnValue(0));
    CleanupOldRotateLogFile();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(strftime).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_FileNameTooShort)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("short.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(0));
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_PrefixMismatch)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("other_20240101010101.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(0));
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_SuffixMismatch)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("virt_ovs_20240101010101.tar")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(0));
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_StrptimeFail)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("virt_ovs_invalid_time.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(0));
    MOCKER(strptime).stubs().will(returnValue((char *)nullptr));
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(strptime).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CleanupOldRotateLogFile_MktimeFail)
{
    DIR *fackDir = reinterpret_cast<DIR *>(0x1234);
    MOCKER(opendir).expects(once()).will(returnValue(fackDir));
    MOCKER(readdir)
        .expects(mockcpp::atLeast(1))
        .will(returnValue(MakeDirent("virt_ovs_20240101010101.tar.gz")))
        .then(returnValue((dirent *)0));
    MOCKER(closedir).expects(once()).will(returnValue(0));
    MOCKER(strftime).stubs().will(returnValue(static_cast<size_t>(1)));
    MOCKER(mktime).stubs().will(returnValue(time_t(-1)));
    MOCKER(unlink).expects(never());

    CleanupOldRotateLogFile();

    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(opendir).reset();
    MOCKER(readdir).reset();
    MOCKER(closedir).reset();
    MOCKER(strftime).reset();
    MOCKER(mktime).reset();
    MOCKER(unlink).reset();
}

TEST_F(TestLogger, CompressOldLogFile_CallCleanup)
{
    MOCKER(CleanupOldRotateLogFile).stubs();
    std::string oldLogFile = "/tmp/test.log";
    std::string ts = "20260123_121314";

    EXPECT_NO_THROW({ CompressOldLogFile(oldLogFile, ts); });
    GlobalMockObject::verify();
    MOCKER(CleanupOldRotateLogFile).reset();
}

TEST_F(TestLogger, RotateLogFile_SizeNotEnough)
{
    MOCKER(GetFileSize).expects(once()).will(returnValue(MAX_LOG_SIZE - 1));
    MOCKER(CompressOldLogFile).expects(never());
    MOCKER(SetFileMode).expects(never());

    Logger::RotateLogFile();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(GetFileSize).reset();
    MOCKER(CompressOldLogFile).reset();
    MOCKER(SetFileMode).reset();
}

TEST_F(TestLogger, RotateLogFile_NormalRotate)
{
    MOCKER(GetFileSize).expects(once()).will(returnValue(MAX_LOG_SIZE + 1));
    MOCKER(NowFilename).expects(once()).will(returnValue(std::string("20240101_010101")));
    MOCKER(SetFileMode).stubs();
    Logger::RotateLogFile();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(GetFileSize).reset();
    MOCKER(NowFilename).reset();
    MOCKER(SetFileMode).reset();
}

TEST_F(TestLogger, InitLogFile_FileOpenFail)
{
    MOCKER(EnsureLogDir).stubs();
    MOCKER(SetFileMode).expects(never());
    InitLogFile();
    GlobalMockObject::verify();
    GlobalMockObject::reset();
    MOCKER(EnsureLogDir).reset();
    MOCKER(SetFileMode).reset();
}

TEST_F(TestLogger, Basename_NullPath)
{
    EXPECT_STREQ(Logger::Basename(nullptr), "");
}

TEST_F(TestLogger, Basename_NoSlash)
{
    EXPECT_STREQ(Logger::Basename("filename.cpp"), "filename.cpp");
}

TEST_F(TestLogger, Basename_WithSlash)
{
    EXPECT_STREQ(Logger::Basename("/path/to/file.cpp"), "file.cpp");
}

TEST_F(TestLogger, Basename_MultipleSlash)
{
    EXPECT_STREQ(Logger::Basename("/a/b/c/d/file.cpp"), "file.cpp");
}

TEST_F(TestLogger, FormatTime_Format)
{
    auto now = std::chrono::system_clock::now();
    Logger entry(LoggerLevel::INFO, "test.cpp", "testFunc", 1);
    std::string formatted = entry.FormatTime(now);
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("."), std::string::npos);
}

TEST_F(TestLogger, GetTid_ReturnValid)
{
    uint64_t tid = Logger::GetTid();
    EXPECT_GT(tid, 0u);
}

TEST_F(TestLogger, LoggerConstructor_Basic)
{
    Logger entry(LoggerLevel::DEBUG, "/test/path.cpp", "testFunc", 10);
    EXPECT_EQ(entry.level_, LoggerLevel::DEBUG);
}

TEST_F(TestLogger, LoggerConstructor_NullFile)
{
    Logger entry(LoggerLevel::INFO, nullptr, "testFunc", 10);
    EXPECT_EQ(entry.level_, LoggerLevel::INFO);
}

TEST_F(TestLogger, LoggerConstructor_AllLevels)
{
    Logger debug(LoggerLevel::DEBUG, "test.cpp", "func", 1);
    Logger info(LoggerLevel::INFO, "test.cpp", "func", 2);
    Logger warn(LoggerLevel::WARN, "test.cpp", "func", 3);
    Logger error(LoggerLevel::ERROR, "test.cpp", "func", 4);

    EXPECT_EQ(debug.level_, LoggerLevel::DEBUG);
    EXPECT_EQ(info.level_, LoggerLevel::INFO);
    EXPECT_EQ(warn.level_, LoggerLevel::WARN);
    EXPECT_EQ(error.level_, LoggerLevel::ERROR);
}

TEST_F(TestLogger, LoggerOperator_Stream)
{
    Logger entry(LoggerLevel::INFO, "test.cpp", "func", 1);
    entry << "test " << 123 << " " << 45.67;
    std::string result = entry.ss_.str();
    EXPECT_NE(result.find("test"), std::string::npos);
    EXPECT_NE(result.find("123"), std::string::npos);
}

TEST_F(TestLogger, SetFileMode_Basic)
{
    MOCKER(chmod).stubs().will(returnValue(0));
    SetFileMode("/tmp/test.log", 0644);
    MOCKER(chmod).reset();
}

TEST_F(TestLogger, SetFileMode_Fail)
{
    MOCKER(chmod).stubs().will(returnValue(-1));
    SetFileMode("/tmp/test.log", 0644);
    MOCKER(chmod).reset();
}
} // namespace ovs::ut