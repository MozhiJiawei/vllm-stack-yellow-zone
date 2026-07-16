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

#include "urma_utility.h"
#include "logger.h"

namespace virt::ovs::ubse::urma {
UrmaUtility &UrmaUtility::Instance()
{
    static UrmaUtility urmaUtility;
    return urmaUtility;
}

UrmaUtility::UrmaUtility()
{
    LoadSymbols(urmaBandWidthGet, "ubs_urma_bandwidth_get");
    LoadSymbols(urmaBandWidthSet, "ubs_urma_bandwidth_set");
    LoadSymbols(urmaBandWidthReset, "ubs_urma_bandwidth_reset");
}

UrmaUtility::~UrmaUtility()
{
    urmaBandWidthReset = nullptr;
    urmaBandWidthGet = nullptr;
    urmaBandWidthSet = nullptr;
}

template <class FunctionPtr>
void UrmaUtility::LoadSymbols(FunctionPtr &ptr, const std::string &symbolName)
{
    if (ptr) {
        return;
    }
    auto &ubseLib = ClientLibrary::Instance();
    void *sym = ubseLib.GetSymbol(symbolName);
    ptr = reinterpret_cast<std::decay_t<decltype(ptr)>>(sym);
}

uint32_t UrmaUtility::GetBandWidth(const std::string &deviceName, uint32_t &minBandwidth, uint32_t &maxBandwidth)
{
    auto ret = urmaBandWidthGet(deviceName.data(), &minBandwidth, &maxBandwidth);
    if (ret != 0) {
        LOG_ERROR << "urmaBandWidthQuery failed, ret= " << ret;
    }
    LOG_DEBUG << "urmaBandWidthQuery device name is " << deviceName << "max bandwidth: " << maxBandwidth
              << "min bandwidth: " << minBandwidth;
    return ret;
}

uint32_t UrmaUtility::SetBandWidth(const std::string &deviceName, uint32_t minBandwidth, uint32_t maxBandwidth)
{
    auto ret = urmaBandWidthSet(deviceName.data(), minBandwidth, maxBandwidth);
    if (ret != 0) {
        LOG_ERROR << "urmaBandWidthSet failed, ret= " << ret;
    }
    LOG_DEBUG << "urmaBandWidthSet device name is " << deviceName << "max bandwidth: " << maxBandwidth
              << "min bandwidth: " << minBandwidth;
    return ret;
}

uint32_t UrmaUtility::ResetBandWidth(const std::string &deviceName)
{
    auto ret = urmaBandWidthReset(deviceName.data());
    if (ret != 0) {
        LOG_ERROR << "urmaBandWidthReset failed, ret= " << ret;
    }
    LOG_DEBUG << "urmaBandWidthReset device name is " << deviceName;
    return ret;
}
} // namespace virt::ovs::ubse::urma
