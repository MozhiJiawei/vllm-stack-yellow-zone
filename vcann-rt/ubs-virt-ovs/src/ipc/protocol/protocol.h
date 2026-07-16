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

#ifndef UBS_VIRT_PROTOCOL_H
#define UBS_VIRT_PROTOCOL_H
#include "virt_ipc_code.h"
#include "virt_msg.h"
namespace virt::ovs {

using namespace virt::ovs::msg;

template <typename RespT>
IpcResponse Ok(const RespT &resp)
{
    VirtMsgPacker packer;
    resp.Serialize(packer);
    return {static_cast<uint32_t>(VirtIPCCode::OK), packer.String()};
}

template <typename RespT>
IpcResponse BizError(VirtIPCCode bizCode, const std::string &msg)
{
    RespT resp(bizCode, msg);
    return Ok(resp);
}

inline IpcResponse IpcError(VirtIPCCode code)
{
    return IpcResponse{static_cast<uint32_t>(code)};
}

template <typename Request>
VirtIPCCode DeserializeAndValidate(Request &req, const std::string &payload, std::string &errMsg)
{
    try {
        VirtMsgUnPacker unpacker(payload);
        req.Deserialize(unpacker);
    } catch (...) {
        errMsg = "Error deserializing request";
        return VirtIPCCode::DESERIALIZE_ERROR;
    }
    return req.Validate(errMsg);
}

} // namespace virt::ovs
#endif // UBS_VIRT_PROTOCOL_H
