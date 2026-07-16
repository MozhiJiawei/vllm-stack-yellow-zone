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

#ifndef DYNAMIC_BITSET_H
#define DYNAMIC_BITSET_H

#include <set>
#include <string>
#include <vector>

namespace vas::common {
using DynamicBitset = std::vector<bool>;

class Bitset {
public:
    static void DynamicBitsetSet(DynamicBitset &bitSet, uint16_t start, uint16_t size);
    static void DynamicBitsetClear(DynamicBitset &bitSet, uint16_t start, uint16_t size);
    static DynamicBitset DynamicBitsetNot(const DynamicBitset &src);
    static void DynamicBitsetOr(DynamicBitset &dst, const DynamicBitset &src);
    static void DynamicBitsetAnd(DynamicBitset &dst, const DynamicBitset &src);
    static bool IsDynamicBitsetCross(const DynamicBitset &first, const DynamicBitset &second);
    static DynamicBitset GenDynamicBitSetByArea(uint16_t totalSize, uint16_t start, uint16_t size);
    static DynamicBitset GenClusterBitSetByCpuSet(const DynamicBitset &bitSet, const std::set<uint16_t> &cpuSet);
    static int16_t FindFirstIdlePos(const DynamicBitset &bitSet, uint16_t start, uint16_t size);
    static std::set<uint16_t> GetDynamicBitsetAreaSet(const DynamicBitset &bitset);
    static DynamicBitset GenDynamicBitsetByCpuSet(uint16_t totalSize, const std::set<uint16_t> &cpuSet);
    static void CpuMaskToDynamicBitset(const unsigned char *cpuMask, uint16_t size, DynamicBitset &bitSet);
    static std::string DynamicBitsetToStr(const DynamicBitset &bitSet);
    static std::string GetCpuSetFromDynamicBitset(const DynamicBitset &cpuBitSet);
    static DynamicBitset DynamicBitsetCut(const DynamicBitset &origin, const uint16_t &start, const uint16_t &size);

private:
    static void DynamicBitsetSetArea(DynamicBitset &bitSet, uint16_t start, uint16_t size, bool val);
};
} // namespace vas::common

#endif // DYNAMIC_BITSET_H
