/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "optimizer/util/file_reader.h"

using namespace std::string_literals;
namespace fs = std::filesystem;

TEST(FileReaderTest, ReadExistingFile)
{
    fs::path tempFile = fs::current_path() / "temp_file.json";

    std::ofstream ofs(tempFile.c_str());
    if (!ofs.is_open()) {
        std::cerr << "无法打开文件 " << tempFile << std::endl;
    }

    ofs.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try {
        ofs << "Line 1";
        std::cout << "文件写入成功: " << tempFile << std::endl;
    } catch (const std::ofstream::failure &e) {
        std::cerr << "写入文件失败 - " << e.what() << std::endl;
        ofs.close();
    }

    ofs.close();
    std::cout << "文件关闭成功: " << tempFile << std::endl;

    FileReader reader(tempFile);
    auto lines = reader.read();
    std::cout << "文件读取成功: " << tempFile << std::endl;

    EXPECT_EQ(lines.size(), 1);

    std::remove(tempFile.c_str());
    std::cout << "文件删除成功: " << tempFile << std::endl;
}

TEST(FileReaderTest, ReadNonExistentFile)
{
    const std::string nonExistentFile = "non_existent.txt";
    EXPECT_THROW(FileReader(nonExistentFile).read(), std::runtime_error);
}

TEST(FileReaderTest, ReadEmptyFile)
{
    const std::string emptyFile = "empty_file.txt";
    std::ofstream ofs(emptyFile);
    ofs.close();

    FileReader reader(emptyFile);
    auto lines = reader.read();

    EXPECT_EQ(lines.size(), 0);

    std::remove(emptyFile.c_str());
}