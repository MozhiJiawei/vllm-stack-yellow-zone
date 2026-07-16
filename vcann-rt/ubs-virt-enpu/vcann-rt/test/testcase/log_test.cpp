/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <dirent.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <sys/stat.h>
#include <cstring>

#include "common.h"
#include "log.h"
#include "npu_manager.h"
#include "securec.h"

static const size_t MOCK_MAX_FILE_SIZE = 1 * 1024 * 1024;
static const int MOCK_MAX_BACKUP_COUNT = 3;
static const int TEST_LINE_NUMBER = 100;
static const useconds_t WAIT_FOR_LOG_WRITE_US = 100000;
static const int TEST_LINE_NUMBER_ONE = 1;
static const char *const MOCK_LOG_DIR = "/tmp/log/enpu/vcann-rt/mock/";
static const char *const MOCK_LOG_PATH = "vCann.log";
static const int MOCK_DIR_MODE = 0755;

static void RemoveMockFiles()
{
    DIR *dir = opendir(g_log_config.log_dir);
    if (dir == nullptr) {
        return;
    }
    struct dirent *entry;
    struct stat statbuf;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char full_path[FILE_PATH_LEN];
        int ret = snprintf_s(full_path, sizeof(full_path), sizeof(full_path) - 1, "%s%s", g_log_config.log_dir,
                             entry->d_name);
        if (ret < 0) {
            closedir(dir);
            return;
        }
        if (stat(full_path, &statbuf) != 0) {
            continue;
        }
        if (S_ISREG(statbuf.st_mode)) {
            remove(full_path);
        }
    }
    closedir(dir);
}

static void MkdirRecursive(const char *path)
{
    std::string tmp(path);
    if (!tmp.empty() && tmp.back() == '/') {
        tmp.pop_back();
    }
    for (size_t i = 1; i < tmp.size(); i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp.c_str(), MOCK_DIR_MODE);
            tmp[i] = '/';
        }
    }
    mkdir(tmp.c_str(), MOCK_DIR_MODE);
}

static void SetLogConfig(const LogConfig *log_config)
{
    strcpy_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), log_config->log_dir);
    strcpy_s(g_log_config.log_path, sizeof(g_log_config.log_path), log_config->log_path);
    strcpy_s(g_log_config.log_file_path, sizeof(g_log_config.log_file_path), log_config->log_file_path);
    g_log_config.max_file_size = log_config->max_file_size;
    g_log_config.max_backup_count = log_config->max_backup_count;
    g_log_config.min_log_level = log_config->min_log_level;
    g_log_config.flush_counter = log_config->flush_counter;
    g_log_config.compress_check_counter = log_config->compress_check_counter;
    g_log_config.compress_disabled = log_config->compress_disabled;
}

class LogTest : public testing::Test {
protected:
    void SetUp() override
    {
        LogConfig mockLogConfig;
        ASSERT_EQ(memset_s(&mockLogConfig, sizeof(LogConfig), 0, sizeof(LogConfig)), 0);
        ASSERT_EQ(strcpy_s(mockLogConfig.log_dir, sizeof(mockLogConfig.log_dir), MOCK_LOG_DIR), 0);
        ASSERT_EQ(strcpy_s(mockLogConfig.log_path, sizeof(mockLogConfig.log_path), MOCK_LOG_PATH), 0);
        mockLogConfig.log_file_path[0] = '\0';
        mockLogConfig.max_file_size = MOCK_MAX_FILE_SIZE;
        mockLogConfig.max_backup_count = MOCK_MAX_BACKUP_COUNT;
        mockLogConfig.min_log_level = ENPU_LOG_INFO;
        mockLogConfig.flush_counter = 0;
        mockLogConfig.compress_check_counter = 0;
        mockLogConfig.compress_disabled = true;
        SetLogConfig(&mockLogConfig);

        MkdirRecursive(MOCK_LOG_DIR);

        enpu_global_init();
    }

    void TearDown() override
    {
        RemoveMockFiles();
    }
};

TEST_F(LogTest, LogTest_log_queue_init)
{
    LogQueue queue;
    memset(&queue, 0, sizeof(LogQueue));
    int ret = log_queue_init(&queue);
    EXPECT_EQ(ret, ENPU_SUCCESS);

    log_queue_destroy(&queue);
}

TEST_F(LogTest, LogTest_log_queue_init_null)
{
    int ret = log_queue_init(NULL);
    EXPECT_EQ(ret, ENPU_FAIL);
}

TEST_F(LogTest, LogTest_log_queue_destroy_null)
{
    log_queue_destroy(NULL);
}

TEST_F(LogTest, LogTest_log_queue_push_pop)
{
    LogQueue queue;
    memset(&queue, 0, sizeof(LogQueue));
    int ret = log_queue_init(&queue);
    EXPECT_EQ(ret, ENPU_SUCCESS);

    LogMessage msg;
    memset(&msg, 0, sizeof(LogMessage));
    msg.level = ENPU_LOG_INFO;
    EXPECT_EQ(strncpy_s(msg.filename, sizeof(msg.filename), "test.cpp", strlen("test.cpp")), 0);
    EXPECT_EQ(strncpy_s(msg.basename, sizeof(msg.basename), "test.cpp", strlen("test.cpp")), 0);
    msg.line = TEST_LINE_NUMBER;
    EXPECT_EQ(strncpy_s(msg.message, sizeof(msg.message), "Test message", strlen("Test message")), 0);
    clock_gettime(CLOCK_REALTIME, &msg.timestamp);

    ret = log_queue_push(&queue, &msg);
    EXPECT_EQ(ret, ENPU_SUCCESS);

    LogMessage popped_msg;
    memset(&popped_msg, 0, sizeof(LogMessage));
    ret = log_queue_pop(&queue, &popped_msg);
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_EQ(popped_msg.level, ENPU_LOG_INFO);
    EXPECT_STREQ(popped_msg.filename, "test.cpp");
    EXPECT_STREQ(popped_msg.basename, "test.cpp");
    EXPECT_EQ(popped_msg.line, TEST_LINE_NUMBER);
    EXPECT_STREQ(popped_msg.message, "Test message");

    log_queue_destroy(&queue);
}

TEST_F(LogTest, LogTest_log_queue_null_args)
{
    LogQueue queue;
    memset(&queue, 0, sizeof(LogQueue));
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    LogMessage msg;
    memset(&msg, 0, sizeof(LogMessage));
    EXPECT_EQ(log_queue_push(NULL, &msg), ENPU_FAIL);
    EXPECT_EQ(log_queue_push(&queue, NULL), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(NULL, &msg), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(&queue, NULL), ENPU_FAIL);
    log_queue_destroy(&queue);
}

TEST_F(LogTest, LogTest_log_queue_after_destroy)
{
    LogQueue queue;
    memset(&queue, 0, sizeof(LogQueue));
    ASSERT_EQ(log_queue_init(&queue), ENPU_SUCCESS);
    log_queue_destroy(&queue);
    LogMessage msg;
    memset(&msg, 0, sizeof(LogMessage));
    EXPECT_EQ(log_queue_push(&queue, &msg), ENPU_FAIL);
    EXPECT_EQ(log_queue_pop(&queue, &msg), ENPU_FAIL);
}

TEST_F(LogTest, LogTest_log_print)
{
    g_log_config.min_log_level = ENPU_LOG_DEBUG;

    LOG_DEBUG("Debug log test");
    LOG_INFO("Info log test");
    LOG_WARN("Warn log test");
    LOG_ERROR("Error log test");
    LOG_FATAL("Fatal log test");

    usleep(WAIT_FOR_LOG_WRITE_US);

    g_log_config.min_log_level = ENPU_LOG_INFO;
}

TEST_F(LogTest, LogTest_log_level_filter)
{
    g_log_config.min_log_level = ENPU_LOG_WARN;

    LOG_DEBUG("This should be filtered");
    LOG_INFO("This should be filtered");
    LOG_WARN("This should appear");
    LOG_ERROR("This should appear");
    LOG_FATAL("This should appear");

    usleep(WAIT_FOR_LOG_WRITE_US);

    g_log_config.min_log_level = ENPU_LOG_INFO;
}

TEST_F(LogTest, LogTest_basename_extraction)
{
    LogMessage msg;
    memset(&msg, 0, sizeof(LogMessage));
    msg.level = ENPU_LOG_INFO;
    msg.line = TEST_LINE_NUMBER_ONE;
    EXPECT_EQ(strncpy_s(msg.message, sizeof(msg.message), "test", strlen("test")), 0);
    clock_gettime(CLOCK_REALTIME, &msg.timestamp);

    EXPECT_EQ(strncpy_s(msg.filename, sizeof(msg.filename), "/path/to/test.cpp", strlen("/path/to/test.cpp")), 0);
    EXPECT_EQ(strncpy_s(msg.basename, sizeof(msg.basename), "test.cpp", strlen("test.cpp")), 0);

    LogQueue queue;
    memset(&queue, 0, sizeof(LogQueue));
    log_queue_init(&queue);

    int ret = log_queue_push(&queue, &msg);
    EXPECT_EQ(ret, ENPU_SUCCESS);

    LogMessage popped;
    memset(&popped, 0, sizeof(LogMessage));
    ret = log_queue_pop(&queue, &popped);
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_STREQ(popped.basename, "test.cpp");

    log_queue_destroy(&queue);
}

static void CreateMockLogFile(const char *name)
{
    char full_path[FILE_PATH_LEN];
    int ret = snprintf_s(full_path, sizeof(full_path), sizeof(full_path) - 1, "%s%s", g_log_config.log_dir, name);
    if (ret < 0) {
        return;
    }
    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        const char *content = "mock log content for testing\n";
        write(fd, content, strlen(content));
        close(fd);
    }
}

TEST_F(LogTest, LogTest_compress_file_basic)
{
    char log_name[FILE_PATH_LEN];
    int ret = snprintf_s(log_name, sizeof(log_name), sizeof(log_name) - 1, "%s_%s_%d_20260401120000%s", MODULE_NAME,
                         SUB_MODULE_NAME, (int)getpid(), LOG_FILE_SUFFIX);
    ASSERT_GE(ret, 0);
    CreateMockLogFile(log_name);

    ret = compress_file();
    EXPECT_EQ(ret, ENPU_SUCCESS);

    DIR *dir = opendir(g_log_config.log_dir);
    ASSERT_NE(dir, nullptr);
    bool found_zip = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strstr(entry->d_name, ZIP_EXT) != nullptr) {
            found_zip = true;
            break;
        }
    }
    closedir(dir);
    EXPECT_TRUE(found_zip);
}

TEST_F(LogTest, LogTest_is_log_file)
{
    char valid_path[FILE_PATH_LEN];
    int ret = snprintf_s(valid_path, sizeof(valid_path), sizeof(valid_path) - 1, "%s%s_%s_%d_20260401120000%s",
                         g_log_config.log_dir, MODULE_NAME, SUB_MODULE_NAME, (int)getpid(), LOG_FILE_SUFFIX);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(is_log_file(valid_path), ENPU_SUCCESS);

    char wrong_suffix_path[FILE_PATH_LEN];
    ret = snprintf_s(wrong_suffix_path, sizeof(wrong_suffix_path), sizeof(wrong_suffix_path) - 1,
                     "%s%s_%s_%d_20260401120000.txt", g_log_config.log_dir, MODULE_NAME, SUB_MODULE_NAME,
                     (int)getpid());
    ASSERT_GE(ret, 0);
    EXPECT_EQ(is_log_file(wrong_suffix_path), ENPU_FAIL);

    char wrong_pid_path[FILE_PATH_LEN];
    ret = snprintf_s(wrong_pid_path, sizeof(wrong_pid_path), sizeof(wrong_pid_path) - 1,
                     "%s%s_%s_99999_20260401120000%s", g_log_config.log_dir, MODULE_NAME, SUB_MODULE_NAME,
                     LOG_FILE_SUFFIX);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(is_log_file(wrong_pid_path), ENPU_FAIL);

    EXPECT_EQ(is_log_file("short.log"), ENPU_FAIL);
}

TEST_F(LogTest, LogTest_update_log_file)
{
    int ret = update_log_file();
    EXPECT_EQ(ret, ENPU_SUCCESS);

    struct stat st;
    EXPECT_EQ(stat(g_log_config.log_path, &st), 0);
    EXPECT_TRUE(S_ISREG(st.st_mode));
}

TEST_F(LogTest, LogTest_log_shutdown_and_reinit)
{
    log_shutdown();
    int ret = log_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
}

TEST_F(LogTest, LogTest_log_init_env_log_level)
{
    log_shutdown();
    setenv("ENPU_LOG_LEVEL", "2", 1);
    int ret = log_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_EQ(g_log_config.min_log_level, ENPU_LOG_WARN);
    unsetenv("ENPU_LOG_LEVEL");

    g_log_config.min_log_level = ENPU_LOG_INFO;
}

TEST_F(LogTest, LogTest_log_init_env_invalid_log_level)
{
    log_shutdown();
    setenv("ENPU_LOG_LEVEL", "invalid", 1);
    int ret = log_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
    EXPECT_EQ(g_log_config.min_log_level, ENPU_LOG_INFO);
    unsetenv("ENPU_LOG_LEVEL");
}

TEST_F(LogTest, LogTest_log_print_not_initialized)
{
    log_shutdown();
    LOG_INFO("This should be silently ignored");
    int ret = log_init();
    EXPECT_EQ(ret, ENPU_SUCCESS);
}
