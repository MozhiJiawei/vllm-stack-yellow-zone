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

#include "urma_service.h"
#include "logger.h"
#include "macros.h"
#include "protocol.h"

namespace virt::ovs::service::urma {

using namespace virt::ovs;

std::string UrmaService::Name() const
{
    return "ubs.urma";
}

UrmaService::UrmaService() : urmaUtil_(UrmaUtility::Instance())
{
    Register("SetBandwidth", &UrmaService::HandleSetBandwidth);
    Register("GetBandwidth", &UrmaService::HandleGetBandwidth);
    Register("ResetBandwidth", &UrmaService::HandleResetBandwidth);
}

IpcResponse UrmaService::HandleSetBandwidth(const std::string &payload)
{
    UrmaBandwidthSetRequest request;
    std::string errMsg;
    VirtIPCCode ret = DeserializeAndValidate(request, payload, errMsg);
    if (ret != VirtIPCCode::OK) {
        LOG_ERROR << "HandleSetBandwidth failed: " << errMsg;
        return BizError<BaseResponse>(ret, errMsg);
    }
    uint32_t res = urmaUtil_.SetBandWidth(request.name_, GbToMb(request.minBandwidth_), GbToMb(request.maxBandwidth_));
    if (res != 0) {
        LOG_ERROR << "set bandwidth failed,name is " << request.name_ << ",res=" << res;
        return BizError<BaseResponse>(VirtIPCCode::UBSE_ERROR, "call ubse failed");
    }
    return Ok(BaseResponse::Success());
}

IpcResponse UrmaService::HandleGetBandwidth(const std::string &payload)
{
    UrmaBandwidthGetRequest request;
    std::string errMsg;
    VirtIPCCode ret = DeserializeAndValidate(request, payload, errMsg);
    if (ret != VirtIPCCode::OK) {
        LOG_ERROR << "HandleGetBandwidth failed: " << errMsg;
        return BizError<UrmaBandwidthGetResponse>(ret, errMsg);
    }
    uint32_t minBandwidth = 0;
    uint32_t maxBandwidth = 0;
    uint32_t res = urmaUtil_.GetBandWidth(request.name_, minBandwidth, maxBandwidth);
    if (res != 0) {
        LOG_ERROR << "get bandwidth from ubse failed,name is " << request.name_ << ",res=" << res;
        return BizError<UrmaBandwidthGetResponse>(VirtIPCCode::UBSE_ERROR, "call ubse failed");
    }
    return Ok(UrmaBandwidthGetResponse(MbToGb(minBandwidth), MbToGb(maxBandwidth)));
}

IpcResponse UrmaService::HandleResetBandwidth(const std::string &payload)
{
    UrmaBandwidthResetRequest request;
    std::string errMsg;
    VirtIPCCode ret = DeserializeAndValidate(request, payload, errMsg);
    if (ret != VirtIPCCode::OK) {
        LOG_ERROR << "HandleResetBandwidth failed: " << errMsg;
        return BizError<BaseResponse>(ret, errMsg);
    }
    uint32_t res = urmaUtil_.ResetBandWidth(request.name_);
    if (res != 0) {
        if (res == static_cast<uint32_t>(VirtIPCCode::NOT_EXIST)) {
            LOG_ERROR << "urma dev is not exist" << request.name_;
            return BizError<BaseResponse>(VirtIPCCode::NOT_EXIST, "urma dev is not exist");
        }
        LOG_ERROR << "reset bandwidth failed,name is " << request.name_ << ",res=" << res;
        return BizError<BaseResponse>(VirtIPCCode::UBSE_ERROR, "call ubse failed");
    }
    return Ok(BaseResponse::Success());
}

REGISTER_SERVICE(UrmaService);

} // namespace virt::ovs::service::urma