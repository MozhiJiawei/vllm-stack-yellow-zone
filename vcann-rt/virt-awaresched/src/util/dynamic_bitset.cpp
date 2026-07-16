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

#include "dynamic_bitset.h"

#include <cstddef>
#include <sstream>

#include "def.h"
#include "logger.h"

namespace vas::common {
/**
 * Set the specified area to 1.
 * @param start
 * @param size
 * @param dynamicBitset
 */
void Bitset::DynamicBitsetSet(DynamicBitset &bitSet, uint16_t start, uint16_t size)
{
    try {
        DynamicBitsetSetArea(bitSet, start, size, true);
    } catch (const std::out_of_range &e) {
        LOG_ERROR(e.what());
    }
}

/**
 * Set the specified area to 0.
 * @param start
 * @param size
 * @param dynamicBitset
 */
void Bitset::DynamicBitsetClear(DynamicBitset &bitSet, uint16_t start, uint16_t size)
{
    try {
        DynamicBitsetSetArea(bitSet, start, size, false);
    } catch (const std::out_of_range &e) {
        LOG_ERROR(e.what());
    }
}

/**
 * Invert dynamicBitset
 * @param origin
 * @return
 */
DynamicBitset Bitset::DynamicBitsetNot(const DynamicBitset &src)
{
    DynamicBitset dst(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        dst[i] = !src[i];
    }
    return dst;
}

/**
 * Perform an OR operation between origin and ret, then assign the result to ret.
 * @param origin
 * @param ret
 */
void Bitset::DynamicBitsetOr(DynamicBitset &dst, const DynamicBitset &src)
{
    if (dst.size() != src.size()) {
        LOG_ERROR("The sizes of the bitsets are not equal.");
        return;
    }
    for (size_t i = 0; i < src.size(); i++) {
        dst[i] = dst[i] | src[i];
    }
}

/**
 * Perform a bitwise AND operation between origin and ret, then assign the result to ret.
 * @param origin
 * @param ret
 */
void Bitset::DynamicBitsetAnd(DynamicBitset &dst, const DynamicBitset &src)
{
    if (dst.size() != src.size()) {
        LOG_ERROR("The sizes of the bitsets are not equal.");
        return;
    }
    for (size_t i = 0; i < src.size(); i++) {
        dst[i] = dst[i] & src[i];
    }
}

/**
 * Check if two dynamic bitsets have an intersection.
 * @param first The first dynamic bitset.
 * @param second The second dynamic bitset.
 * @return Returns true if both bitsets have a 1 at the same index; otherwise, returns false.
 */
bool Bitset::IsDynamicBitsetCross(const DynamicBitset &first, const DynamicBitset &second)
{
    unsigned long size = std::min(first.size(), second.size());
    for (int i = 0; i < size; ++i) {
        if (first[i] == 1 && first[i] == second[i]) {
            return true;
        }
    }
    return false;
}

/**
 * Return a dynamicBitset which has a contiguous block of 1s based on length.
 * @param totalSize
 * @param start
 * @param size
 * @return DynamicBitset
 */
DynamicBitset Bitset::GenDynamicBitSetByArea(uint16_t totalSize, uint16_t start, uint16_t size)
{
    DynamicBitset bitSet(totalSize);
    DynamicBitsetSet(bitSet, start, size);
    return bitSet;
}

/**
 * Return a dynamicBitset which has a cpuSet-index range.
 * @param bitSet
 * @param cpuSet
 * @return DynamicBitset
 */
DynamicBitset Bitset::GenClusterBitSetByCpuSet(const DynamicBitset &bitSet, const std::set<uint16_t> &cpuSet)
{
    return {bitSet.begin() + *cpuSet.begin(), bitSet.begin() + *cpuSet.rbegin() + 1};
}

/**
 * Find the next contiguous free area From the bitmap search.
 * @param bitSet
 * @param size
 * @param start
 * @return int16_t first idle position
 */
int16_t Bitset::FindFirstIdlePos(const DynamicBitset &bitSet, uint16_t start, uint16_t size)
{
    if (size == 0 || bitSet.size() < static_cast<size_t>(start) ||
        bitSet.size() - static_cast<size_t>(start) < static_cast<size_t>(size)) {
        return -1;
    }

    int16_t result = -1;
    int16_t idleCount = 0;
    for (size_t i = start; i < bitSet.size(); ++i) {
        if (idleCount == 0 && (bitSet.size() - i < size)) {
            return -1;
        }

        if (!bitSet[i]) {
            if (idleCount == 0) {
                result = static_cast<int16_t>(i);
            }
            ++idleCount;
        }

        if (idleCount == size) {
            return result;
        }
    }
    return -1;
}

/**
 * Find true bit location set in dynamic bitset
 * @param bitset
 */
std::set<uint16_t> Bitset::GetDynamicBitsetAreaSet(const DynamicBitset &bitset)
{
    std::set<uint16_t> locationSet{};
    for (int i = 0; i < bitset.size(); ++i) {
        if (bitset[i]) {
            locationSet.emplace(i);
        }
    }
    return locationSet;
}

/**
 * Generate a dynamic bitset based on a set of CPU IDs.
 * @param totalSize The total size of the dynamic bitset.
 * @param cpuSet A set of CPU IDs to be set to 1 in the bitset.
 * @return A dynamic bitset where the bits corresponding to the CPU IDs in the set are set to 1.
 */
DynamicBitset Bitset::GenDynamicBitsetByCpuSet(uint16_t totalSize, const std::set<uint16_t> &cpuSet)
{
    DynamicBitset bitSet(totalSize);
    for (auto &cpuId : cpuSet) {
        DynamicBitsetSet(bitSet, cpuId, 1);
    }
    return bitSet;
}

/**
 * CpuMap to dynamicBitset
 * @param cpuMap
 * @param maplen
 * @param dynamicBitset
 */
void Bitset::CpuMaskToDynamicBitset(const unsigned char *cpuMask, uint16_t size, DynamicBitset &bitSet)
{
    bitSet.reserve(size * BYTE);
    for (int i = 0; i < size; ++i) {
        const unsigned char byte = cpuMask[i];
        for (int bit = 0; bit <= BYTE - 1; ++bit) {
            bitSet.push_back((byte >> bit) & 1);
        }
    }
}

/**
 * DynamicBitset to str
 * @param bitSet
 * @return
 */
std::string Bitset::DynamicBitsetToStr(const DynamicBitset &bitSet)
{
    std::ostringstream oss;
    for (const auto bit : bitSet) {
        oss << (bit ? '1' : '0');
    }
    return oss.str();
}

/**
 * Get cpuSet from dynamicBitset
 * @param cpuBitSet
 * @return
 */
std::string Bitset::GetCpuSetFromDynamicBitset(const DynamicBitset &cpuBitSet)
{
    std::ostringstream oss{};
    for (size_t i = 0; i < cpuBitSet.size(); i++) {
        if (cpuBitSet[i]) {
            oss << i << " ";
        }
    }
    std::string result = oss.str();
    if (!result.empty()) {
        result.pop_back();
    } else {
        result = "";
    }
    return result;
}

DynamicBitset Bitset::DynamicBitsetCut(const DynamicBitset &origin, const uint16_t &start, const uint16_t &size)
{
    DynamicBitset ret(size);
    for (size_t i = 0; i < size && (start + i) < origin.size(); ++i) {
        ret[i] = origin[start + i];
    }
    return ret;
}

void Bitset::DynamicBitsetSetArea(DynamicBitset &bitSet, uint16_t start, uint16_t size, bool val)
{
    if (size == 0 || bitSet.size() - static_cast<size_t>(start) < static_cast<size_t>(size)) {
        throw std::out_of_range("End value out of range for uint16_t in range: " + std::to_string(bitSet.size()));
    }
    for (size_t index = start; index < start + size; ++index) {
        bitSet[index] = val;
    }
}
} // namespace vas::common
