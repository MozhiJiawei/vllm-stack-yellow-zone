/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "string_util.h"

#include <charconv>
#include <random>

namespace vas::common {
pid_t StringUtil::StringToPidt(const char *in)
{
    if (in == nullptr) {
        throw std::invalid_argument("Input string is nullptr");
    }
    std::string str(in);
    if (str.empty()) {
        throw std::invalid_argument("Input string is empty");
    }
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument("Input string contains non-digit characters");
        }
    }
    unsigned long val;
    try {
        val = std::stoul(str);
        if (val > std::numeric_limits<pid_t>::max()) {
            throw std::out_of_range("Input string exceeds the maximum value for pid_t");
        }
    } catch (const std::invalid_argument &e) {
        throw std::invalid_argument("Invalid argument: '" + str + "' to pid_t: " + e.what());
    } catch (const std::out_of_range &e) {
        throw std::out_of_range("Out of range: " + str);
    }
    return static_cast<pid_t>(val);
}

uint16_t StringUtil::StringToUint16(const char *in)
{
    if (in == nullptr) {
        throw std::invalid_argument("Input string is nullptr");
    }
    std::string str(in);
    if (str.empty()) {
        throw std::invalid_argument("Input string is empty");
    }
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument("Input string contains non-digit characters");
        }
    }
    unsigned long val;
    try {
        val = std::stoul(str);
        if (val > std::numeric_limits<uint16_t>::max()) {
            throw std::out_of_range("Input string exceeds the maximum value for uint16_t");
        }
    } catch (const std::invalid_argument &e) {
        throw std::invalid_argument("Invalid argument: '" + str + "' to uint16_t: " + e.what());
    } catch (const std::out_of_range &e) {
        throw std::out_of_range("Out of range: " + str);
    }

    return static_cast<uint16_t>(val);
}

std::string StringUtil::Trim(const std::string &str)
{
    if (str.empty()) {
        return str;
    }

    auto start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }

    auto end = str.find_last_not_of(" \t\n\r\f\v");

    return str.substr(start, end - start + 1);
}

/**
 * string to vector
 * exp: "0-1,5,7-8" -> [0,1,5,7,8]
 * @param line
 * @return
 */
std::set<uint16_t> StringUtil::ParseStringRange(const std::string &line)
{
    std::set<uint16_t> cpus;
    std::stringstream ss(line);
    std::string tmp;
    while (getline(ss, tmp, ',')) {
        tmp = Trim(tmp); // Remove leading and trailing spaces
        if (tmp.empty()) {
            continue;
        }
        if (const size_t dashPos = tmp.find('-'); dashPos != std::string::npos) {
            std::string startStr = Trim(tmp.substr(0, dashPos));
            std::string endStr = Trim(tmp.substr(dashPos + 1));
            uint16_t start;
            uint16_t end;
            auto [ptr, ec] = std::from_chars(startStr.data(), startStr.data() + startStr.size(), start);
            if (ec == std::errc::invalid_argument || ptr != startStr.data() + startStr.size()) {
                throw std::invalid_argument("Invalid argument in range start: " + startStr);
            } else if (ec == std::errc::result_out_of_range) {
                throw std::out_of_range("Start value out of range for uint16_t in range: " + startStr);
            }

            auto [ptr2, ec2] = std::from_chars(endStr.data(), endStr.data() + endStr.size(), end);
            if (ec2 == std::errc::invalid_argument || ptr2 != endStr.data() + endStr.size()) {
                throw std::invalid_argument("Invalid argument in range end: " + endStr);
            } else if (ec2 == std::errc::result_out_of_range) {
                throw std::out_of_range("End value out of range for uint16_t in range: " + endStr);
            }
            if (start > end) {
                throw std::invalid_argument("Start value is greater than end value in range: " + tmp);
            }
            for (auto i = start; i <= end; ++i) {
                cpus.emplace(i);
            }
        } else {
            uint16_t value;
            auto [ptr, ec] = std::from_chars(tmp.data(), tmp.data() + tmp.size(), value);
            if (ec == std::errc::invalid_argument || ptr != tmp.data() + tmp.size()) {
                throw std::invalid_argument("Invalid argument in single value: " + tmp);
            } else if (ec == std::errc::result_out_of_range) {
                throw std::out_of_range("Value out of range for uint16_t in single value: " + tmp);
            }
            cpus.emplace(value);
        }
    }
    return cpus;
}
} // namespace vas::common