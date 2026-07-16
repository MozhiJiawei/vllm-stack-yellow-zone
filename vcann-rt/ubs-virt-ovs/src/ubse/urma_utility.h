/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef URMA_UTILITY_H
#define URMA_UTILITY_H

#include "client_library.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace virt::ovs::ubse::urma {
using namespace virt::ovs::ubse::client;

class UrmaUtility {
public:
    static UrmaUtility &Instance();

    explicit UrmaUtility();

    // get urma bandwidth
    uint32_t GetBandWidth(const std::string &deviceName, uint32_t &minBandwidth, uint32_t &maxBandwidth);

    // set urma bandwidth
    uint32_t SetBandWidth(const std::string &deviceName, uint32_t minBandwidth, uint32_t maxBandwidth);

    // clear urma bandwidth config
    uint32_t ResetBandWidth(const std::string &deviceName);

private:
    ~UrmaUtility();

    using UbsUrmaBandWidthGetFunc = uint32_t (*)(const char *name, uint32_t *minBandwidth, uint32_t *maxBandwidth);
    using UbsUrmaBandWidthSetFunc = uint32_t (*)(const char *name, uint32_t minBandwidth, uint32_t maxBandwidth);
    using UbsUrmaBandWidthResetFunc = uint32_t (*)(const char *name);

    UbsUrmaBandWidthGetFunc urmaBandWidthGet{nullptr};
    UbsUrmaBandWidthSetFunc urmaBandWidthSet{nullptr};
    UbsUrmaBandWidthResetFunc urmaBandWidthReset{nullptr};

    template <class FunctionPtr>
    void LoadSymbols(FunctionPtr &ptr, const std::string &symbolName);
};
} // namespace virt::ovs::ubse::urma

#endif // URMA_UTILITY_H
