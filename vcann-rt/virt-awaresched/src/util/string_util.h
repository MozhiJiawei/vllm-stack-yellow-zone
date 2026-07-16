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

#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace vas::common {

class StringUtil {
public:
    static pid_t StringToPidt(const char *in);
    static uint16_t StringToUint16(const char *in);
    static std::string Trim(const std::string &str);
    static std::set<uint16_t> ParseStringRange(const std::string &line);
    template <typename T>
    static std::string ObjVecToStr(const std::vector<T> &vec, const std::string &delimiter = ", ")
    {
        if (vec.empty()) {
            return "[]";
        }

        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < vec.size(); ++i) {
            oss << vec[i].ToStr();
            if (i != vec.size() - 1) {
                oss << delimiter;
            }
        }
        oss << "]";

        return oss.str();
    }
    template <typename T>
    static std::string SetToStr(const std::set<T> &vec, const std::string &delimiter = ", ")
    {
        if (vec.empty()) {
            return "[]";
        }

        std::ostringstream oss;
        oss << "[";
        for (auto item = vec.begin(); item != vec.end(); ++item) {
            oss << *item;
            if (std::next(item) != vec.end()) {
                oss << delimiter;
            }
        }
        oss << "]";

        return oss.str();
    }
};

} // namespace vas::common

#endif // STRING_UTIL_H
