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
#include "test_string_util.h"

#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace vas::common;

namespace vas::ut::util {

TEST_F(TestStringUtil, testStringToPidtNormal)
{
    EXPECT_EQ(StringUtil::StringToPidt("123"), 123);
    EXPECT_EQ(StringUtil::StringToPidt("0"), 0);
}

TEST_F(TestStringUtil, testStringToPidtInvalid)
{
    EXPECT_THROW(StringUtil::StringToPidt(""), std::invalid_argument);
    EXPECT_THROW(StringUtil::StringToPidt(nullptr), std::invalid_argument);
    EXPECT_THROW(StringUtil::StringToPidt("abc"), std::invalid_argument);
    pid_t maxPid = std::numeric_limits<pid_t>::max();
    unsigned long val = static_cast<unsigned long>(maxPid) + 1;
    std::string str = std::to_string(val);
    EXPECT_THROW(StringUtil::StringToPidt(str.c_str()), std::out_of_range);
}

TEST_F(TestStringUtil, testStringToUint16Normal)
{
    EXPECT_EQ(StringUtil::StringToUint16("123"), 123);
    EXPECT_EQ(StringUtil::StringToUint16("0"), 0);
}

TEST_F(TestStringUtil, testStringToUint16Invalid)
{
    EXPECT_THROW(StringUtil::StringToUint16(""), std::invalid_argument);
    EXPECT_THROW(StringUtil::StringToUint16(nullptr), std::invalid_argument);
    EXPECT_THROW(StringUtil::StringToUint16("abc"), std::invalid_argument);

    uint16_t maxUint16 = std::numeric_limits<uint16_t>::max();
    uint32_t val = static_cast<uint32_t>(maxUint16) + 1;
    std::string str = std::to_string(val);
    EXPECT_THROW(StringUtil::StringToUint16(str.c_str()), std::out_of_range);
}

TEST_F(TestStringUtil, testTrimNormal)
{
    EXPECT_STREQ(StringUtil::Trim("  hello  ").c_str(), "hello");
    EXPECT_STREQ(StringUtil::Trim("\thello\n").c_str(), "hello");
    EXPECT_STREQ(StringUtil::Trim("hello").c_str(), "hello");
}

TEST_F(TestStringUtil, testTrimAllWhitespace)
{
    EXPECT_STREQ(StringUtil::Trim("   ").c_str(), "");
}

TEST_F(TestStringUtil, testParseStringRangeNormal)
{
    EXPECT_TRUE(StringUtil::ParseStringRange(" ").empty());
    std::set<uint16_t> result = StringUtil::ParseStringRange("0-2,4,6-8");
    std::set<uint16_t> expected = {0, 1, 2, 4, 6, 7, 8};
    EXPECT_EQ(result, expected);
}

TEST_F(TestStringUtil, testParseStringRangeInvalid)
{
    EXPECT_THROW(StringUtil::ParseStringRange("3-1"), std::invalid_argument);
    EXPECT_THROW(StringUtil::ParseStringRange("abc"), std::invalid_argument);
    EXPECT_THROW(StringUtil::ParseStringRange("100000"), std::out_of_range);
}

TEST_F(TestStringUtil, testObjVecToStrNormal)
{
    struct TestObj {
        std::string ToStr() const
        {
            return "test";
        }
    };
    std::vector<TestObj> vec = {TestObj(), TestObj()};
    EXPECT_STREQ(StringUtil::ObjVecToStr(vec).c_str(), "[test, test]");
}

TEST_F(TestStringUtil, testSetToStrNormal)
{
    std::set<int> s = {1, 2, 3};
    EXPECT_STREQ(StringUtil::SetToStr(s).c_str(), "[1, 2, 3]");
}

TEST_F(TestStringUtil, testSetToStrEmpty)
{
    std::set<int> s;
    EXPECT_STREQ(StringUtil::SetToStr(s).c_str(), "[]");
}

} // namespace vas::ut::util