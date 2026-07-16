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

#ifndef UBS_VIRT_OVS_URMA_SERVICE_H
#define UBS_VIRT_OVS_URMA_SERVICE_H
#include "common/constants.h"
#include "service_base.h"
#include "urma_utility.h"
namespace virt::ovs::service::urma {

using namespace virt::ovs::msg;
using namespace virt::ovs::ubse::urma;
using namespace virt::ovs::constants;

class UrmaService : public Service {
public:
    std::string Name() const override;
    UrmaService();

private:
    IpcResponse HandleSetBandwidth(const std::string &payload);
    IpcResponse HandleResetBandwidth(const std::string &payload);
    IpcResponse HandleGetBandwidth(const std::string &payload);

private:
    UrmaUtility &urmaUtil_;

    static uint32_t GbToMb(uint32_t bw)
    {
        return bw * GB_TO_MB;
    }

    static uint32_t MbToGb(uint32_t bw)
    {
        return bw / GB_TO_MB;
    }
};

struct UrmaBandwidthSetRequest : MsgBase {
    std::string name_;
    uint32_t minBandwidth_{};
    uint32_t maxBandwidth_{};

    UrmaBandwidthSetRequest() = default;

    UrmaBandwidthSetRequest(const std::string &name, const uint32_t minBandwidth, const uint32_t maxBandwidth)
        : name_(name),
          minBandwidth_(minBandwidth),
          maxBandwidth_(maxBandwidth)
    {
    }

    void Serialize(VirtMsgPacker &packer) const override
    {
        packer.Serialize(name_);
        packer.Serialize(minBandwidth_);
        packer.Serialize(maxBandwidth_);
    }

    void Deserialize(VirtMsgUnPacker &unpacker) override
    {
        unpacker.Deserialize(name_);
        unpacker.Deserialize(minBandwidth_);
        unpacker.Deserialize(maxBandwidth_);
    }

    VirtIPCCode Validate(std::string &errMsg) override
    {
        constexpr uint32_t nameMaxLength = 31;
        constexpr uint32_t minMaxLength = 1;
        constexpr uint32_t maxBandwidth = 50;
        if (name_.empty() || name_.size() > nameMaxLength) {
            errMsg = "Invalid bandwidth name,length must be 1~31 characters";
            return VirtIPCCode::INVALID_PARAM;
        }
        if (minBandwidth_ < minMaxLength || minBandwidth_ > maxBandwidth) {
            errMsg = "Invalid bandwidth minimum,must be in range[1,50]";
            return VirtIPCCode::INVALID_PARAM;
        }
        if (maxBandwidth_ < minMaxLength || maxBandwidth_ > maxBandwidth) {
            errMsg = "Invalid bandwidth maximum,must be in range[1,50]";
            return VirtIPCCode::INVALID_PARAM;
        }
        if (minBandwidth_ > maxBandwidth_) {
            errMsg = "minBandwidth cannot be greater than maxBandwidth";
            return VirtIPCCode::INVALID_PARAM;
        }
        return VirtIPCCode::OK;
    }
};

struct UrmaBandwidthGetRequest : MsgBase {
    std::string name_;

    UrmaBandwidthGetRequest() = default;

    explicit UrmaBandwidthGetRequest(const std::string &name) : name_(name) {}

    void Serialize(VirtMsgPacker &packer) const override
    {
        packer.Serialize(name_);
    }

    void Deserialize(VirtMsgUnPacker &unpacker) override
    {
        unpacker.Deserialize(name_);
    }

    VirtIPCCode Validate(std::string &errMsg) override
    {
        constexpr uint32_t nameMaxLength = 31;
        if (name_.empty() || name_.size() > nameMaxLength) {
            errMsg = "Invalid bandwidth name,length must be 1~31 characters";
            return VirtIPCCode::INVALID_PARAM;
        }
        return VirtIPCCode::OK;
    }
};

struct UrmaBandwidthResetRequest : UrmaBandwidthGetRequest {
};

struct UrmaBandwidthGetResponse : BaseResponse {
    uint32_t minBandwidth_{};
    uint32_t maxBandwidth_{};

    void Serialize(VirtMsgPacker &packer) const override
    {
        packer.Serialize(ret_);
        packer.Serialize(message_);
        packer.Serialize(minBandwidth_);
        packer.Serialize(maxBandwidth_);
    }

    void Deserialize(VirtMsgUnPacker &unpacker) override
    {
        unpacker.Deserialize(ret_);
        unpacker.Deserialize(message_);
        unpacker.Deserialize(minBandwidth_);
        unpacker.Deserialize(maxBandwidth_);
    }
    UrmaBandwidthGetResponse() = default;

    UrmaBandwidthGetResponse(const VirtIPCCode ret, const std::string &message)
    {
        ret_ = ret;
        message_ = message;
    }

    UrmaBandwidthGetResponse(const uint32_t minBandwidth, const uint32_t maxBandwidth)
        : minBandwidth_(minBandwidth),
          maxBandwidth_(maxBandwidth)
    {
        ret_ = VirtIPCCode::OK;
    }
};

} // namespace virt::ovs::service::urma
#endif // UBS_VIRT_OVS_URMA_SERVICE_H
