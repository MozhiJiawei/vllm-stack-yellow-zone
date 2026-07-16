/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "file_reader.h"

#include <fstream>
#include <stdexcept>

FileReader::FileReader(const std::string &filePath) : filePath_(filePath) {}

std::vector<std::string> FileReader::read() const
{
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        throw std::runtime_error("Open file failed: " + filePath_);
    }

    std::vector<std::string> lines;
    std::string line;
    if (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}