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
#include "test_dynamic_bitset.h"

#include <set>
#include <string>
#include <vector>

#include "dynamic_bitset.h"

using namespace vas::common;

namespace vas::ut::util {
uint16_t g_bitsetLen = 5;
uint16_t g_areaStart = 1;
uint16_t g_areaLen = 3;
uint16_t g_idleStart = 0;
uint16_t g_idleLen = 2;

void TestDynamicBitset::SetUp()
{
    testing::Test::SetUp();
}

void TestDynamicBitset::TearDown()
{
    testing::Test::TearDown();
}

TEST_F(TestDynamicBitset, testDynamicBitsetSet)
{
    DynamicBitset bitSet(g_bitsetLen, false);
    Bitset::DynamicBitsetSet(bitSet, g_areaStart, g_areaLen);
    EXPECT_EQ(bitSet, DynamicBitset({false, true, true, true, false}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetSetArea)
{
    DynamicBitset bitSet(g_bitsetLen, false);
    Bitset::DynamicBitsetSetArea(bitSet, g_areaStart, g_areaLen, true);
    EXPECT_EQ(bitSet, DynamicBitset({false, true, true, true, false}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetClear)
{
    DynamicBitset bitSet(g_bitsetLen, true);
    Bitset::DynamicBitsetClear(bitSet, g_areaStart, g_areaLen);
    EXPECT_EQ(bitSet, DynamicBitset({true, false, false, false, true}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetNot)
{
    const DynamicBitset src({true, false, true});
    const DynamicBitset dst = Bitset::DynamicBitsetNot(src);
    EXPECT_EQ(dst, DynamicBitset({false, true, false}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetOr)
{
    DynamicBitset src1({true, false, true});
    const DynamicBitset src2({false, true, false});
    Bitset::DynamicBitsetOr(src1, src2);
    EXPECT_EQ(src1, DynamicBitset({true, true, true}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetAnd)
{
    DynamicBitset src1({true, false, true});
    const DynamicBitset src2({false, true, true});
    Bitset::DynamicBitsetAnd(src1, src2);
    EXPECT_EQ(src1, DynamicBitset({false, false, true}));
}

TEST_F(TestDynamicBitset, testIsDynamicBitsetCross)
{
    const DynamicBitset first({true, false, true});
    const DynamicBitset second({false, true, true});
    EXPECT_TRUE(Bitset::IsDynamicBitsetCross(first, second));
}

TEST_F(TestDynamicBitset, testGenDynamicBitSetByArea)
{
    const DynamicBitset bitSet = Bitset::GenDynamicBitSetByArea(g_bitsetLen, g_areaStart, g_areaLen);
    EXPECT_EQ(bitSet.size(), g_bitsetLen);
    EXPECT_EQ(bitSet, DynamicBitset({false, true, true, true, false}));
}

TEST_F(TestDynamicBitset, testFindFirstIdlePos1)
{
    const DynamicBitset bitSet({true, false, true, true, false});
    EXPECT_EQ(Bitset::FindFirstIdlePos(bitSet, g_idleStart, g_idleLen), 1);
}

TEST_F(TestDynamicBitset, testFindFirstIdlePos2)
{
    const DynamicBitset bitSet({true, true, true, true, false});
    EXPECT_EQ(Bitset::FindFirstIdlePos(bitSet, g_idleStart, g_idleLen), -1);
}

TEST_F(TestDynamicBitset, testGetDynamicBitsetAreaSet)
{
    const DynamicBitset bitSet({true, false, true, false, true});
    const auto areaSet = Bitset::GetDynamicBitsetAreaSet(bitSet);
    const auto result = std::set<uint16_t>({0, 2, 4});
    EXPECT_EQ(areaSet, result);
}

TEST_F(TestDynamicBitset, testGenDynamicBitsetByCpuSet)
{
    const std::set<uint16_t> cpuSet = {0, 2, 4};
    const DynamicBitset bitSet = Bitset::GenDynamicBitsetByCpuSet(g_bitsetLen, cpuSet);
    EXPECT_EQ(bitSet, DynamicBitset({true, false, true, false, true}));
}

TEST_F(TestDynamicBitset, testCpuMaskToDynamicBitset)
{
    constexpr unsigned char cpuMask[] = {0b1001};
    constexpr uint16_t byteCount = 1;
    DynamicBitset bitSet;
    Bitset::CpuMaskToDynamicBitset(cpuMask, byteCount, bitSet);
    EXPECT_EQ(bitSet, DynamicBitset({true, false, false, true, false, false, false, false}));
}

TEST_F(TestDynamicBitset, testDynamicBitsetToStr)
{
    const DynamicBitset bitSet({true, false, true, false, true});
    const auto str = Bitset::DynamicBitsetToStr(bitSet);
    EXPECT_EQ(str, "10101");
}

TEST_F(TestDynamicBitset, testGetCpuSetFromDynamicBitset)
{
    const DynamicBitset bitSet({true, false, true, false, true});
    const auto cpuSet = Bitset::GetCpuSetFromDynamicBitset(bitSet);
    EXPECT_EQ(cpuSet, "0 2 4");
}
} // namespace vas::ut::util