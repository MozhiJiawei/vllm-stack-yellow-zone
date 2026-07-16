/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "server/data_receiver.h"

TEST(DataReceiverTest, SaveTest)
{
    size_t bufferSize = 3;
    int flushIntervalSec = 5;
    std::string outputPath = "/xxx";
    if (std::filesystem::exists(outputPath)) {
        std::filesystem::remove(outputPath);
    }
    std::shared_ptr<MutexContext> ctx2 = std::make_shared<MutexContext>();

    DataReceiver receiver(ctx2, bufferSize, flushIntervalSec, outputPath);
    std::string a = "abcd";
    receiver.save(a);
    bool containsAbcd = std::find(receiver.jsonBuffer.begin(), receiver.jsonBuffer.end(), "abcd") !=
                        receiver.jsonBuffer.end();
    EXPECT_TRUE(containsAbcd);
    receiver.jsonBuffer.clear();
}

TEST(DataReceiverTest, WriteToDisk)
{
    std::string output_path = "test_output.json";
    std::ofstream create_file(output_path);
    auto mutexContext = std::make_shared<MutexContext>();
    DataReceiver receiver(mutexContext, 2, 1, output_path);

    receiver.jsonBuffer = {"data1", "data2"};

    receiver.writeToDisk();

    std::ifstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "无法打开文件！" << std::endl;
    }

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    EXPECT_EQ(lines.size(), 2);
    std::filesystem::file_status status = std::filesystem::status(output_path);
    EXPECT_TRUE(std::filesystem::is_regular_file(status));
    EXPECT_TRUE((status.permissions() & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    EXPECT_TRUE((status.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none);

    std::filesystem::remove(output_path);
}

TEST(DataReceiverTest, WriteToDisk_CreateSingleParentDir)
{
    auto mutexContext = std::make_shared<MutexContext>();
    DataReceiver receiver(mutexContext, 2, 1, "test_output.json");

    std::filesystem::path outputPath("test_output.json");
    std::filesystem::remove_all(outputPath.parent_path());

    receiver.jsonBuffer = {"data1", "data2"};

    receiver.writeToDisk();

    std::ifstream file("test_output.json");
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    EXPECT_EQ(lines.size(), 2);

    std::filesystem::file_status status = std::filesystem::status(outputPath.parent_path());
    EXPECT_TRUE((status.permissions() & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    EXPECT_TRUE((status.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none);

    std::filesystem::remove(outputPath);
    std::filesystem::remove_all(outputPath.parent_path());
}