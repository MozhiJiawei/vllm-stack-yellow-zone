/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "test_urma_service.h"
#include <dlfcn.h>

namespace ovs::ut {
using namespace virt::ovs;

void TestUrmaService::SetUp()
{
    Test::SetUp();
    std::shared_ptr<int> fake = std::make_shared<int>(1);
    void *fakeHandler = static_cast<void *>(fake.get());
    MOCKER_CPP(dlopen).stubs().will(returnValue(fakeHandler));
    MOCKER_CPP(dlsym).stubs().will(returnValue(fakeHandler));
    service = std::make_shared<UrmaService>();
}

void TestUrmaService::TearDown()
{
    Test::TearDown();
    MOCKER_CPP(dlopen).reset();
    MOCKER_CPP(dlsym).reset();
}

TEST_F(TestUrmaService, HandleSetBandwidth_DeserializeFail)
{
    std::string badPayload = "invalid-req";
    IpcResponse resp = service->HandleSetBandwidth(badPayload);
    EXPECT_EQ(resp.code_, 0);

    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::INVALID_PARAM);
}

TEST_F(TestUrmaService, HandleSetBandwidth_Success)
{
    UrmaBandwidthSetRequest req;
    req.name_ = "urma0";
    req.minBandwidth_ = minBandwidth;
    req.maxBandwidth_ = maxBandwidth;

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::SetBandWidth)
        .expects(once())
        .with(eq(std::string("urma0")), eq(minBandwidth * GB_TO_MB), eq(maxBandwidth * GB_TO_MB))
        .will(returnValue(uint32_t(0)));

    IpcResponse resp = service->HandleSetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::OK);
    MOCKER(&UrmaUtility::SetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleSetBandwidth_UbseFail)
{
    UrmaBandwidthSetRequest req;
    req.name_ = "urma0";
    req.minBandwidth_ = minBandwidth;
    req.maxBandwidth_ = maxBandwidth;

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::SetBandWidth).expects(once()).will(returnValue(uint32_t(1)));

    IpcResponse resp = service->HandleSetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::UBSE_ERROR);
    MOCKER(&UrmaUtility::SetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleGetBandwidth_DeserializeFail)
{
    std::string badPayload = "invalid-req";
    IpcResponse resp = service->HandleGetBandwidth(badPayload);
    EXPECT_EQ(resp.code_, 0);

    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::INVALID_PARAM);
}

uint32_t FakeGetBandwidth(UrmaUtility *_mockClass, const std::string &, uint32_t &minBw, uint32_t &maxBw)
{
    minBw = minBandwidth * GB_TO_MB;
    maxBw = maxBandwidth * GB_TO_MB;
    return 0;
}

TEST_F(TestUrmaService, HandleGetBandwidth_Success)
{
    UrmaBandwidthGetRequest req;
    req.name_ = "urma0";

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::GetBandWidth)
        .expects(once())
        .with(eq(std::string("urma0")), _, _)
        .will(invoke(FakeGetBandwidth));

    IpcResponse resp = service->HandleGetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    UrmaBandwidthGetResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::OK);
    EXPECT_EQ(acResp.minBandwidth_, minBandwidth);
    EXPECT_EQ(acResp.maxBandwidth_, maxBandwidth);
    MOCKER(&UrmaUtility::GetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleGetBandwidth_UbseFail)
{
    UrmaBandwidthGetRequest req;
    req.name_ = "urma0";

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::GetBandWidth).expects(once()).will(returnValue(uint32_t(1)));

    IpcResponse resp = service->HandleGetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    UrmaBandwidthGetResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::UBSE_ERROR);
    MOCKER(&UrmaUtility::GetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleResetBandwidth_DeserializeFail)
{
    std::string badPayload = "invalid-req";
    IpcResponse resp = service->HandleResetBandwidth(badPayload);
    EXPECT_EQ(resp.code_, 0);

    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::INVALID_PARAM);
}

TEST_F(TestUrmaService, HandleResetBandwidth_Success)
{
    UrmaBandwidthResetRequest req;
    req.name_ = "urma0";

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::ResetBandWidth).expects(once()).with(eq(std::string("urma0"))).will(returnValue(uint32_t(0)));

    IpcResponse resp = service->HandleResetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::OK);
    MOCKER(&UrmaUtility::ResetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleResetBandwidth_NotExist)
{
    UrmaBandwidthResetRequest req;
    req.name_ = "urma0";

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::ResetBandWidth)
        .expects(once())
        .will(returnValue(static_cast<uint32_t>(VirtIPCCode::NOT_EXIST)));

    IpcResponse resp = service->HandleResetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::NOT_EXIST);
    MOCKER(&UrmaUtility::ResetBandWidth).reset();
}

TEST_F(TestUrmaService, HandleResetBandwidth_UbseFail)
{
    UrmaBandwidthResetRequest req;
    req.name_ = "urma0";

    VirtMsgPacker packer;
    req.Serialize(packer);
    std::string payload = packer.String();
    MOCKER(&UrmaUtility::ResetBandWidth).expects(once()).will(returnValue(uint32_t(1)));

    IpcResponse resp = service->HandleResetBandwidth(payload);
    EXPECT_EQ(resp.code_, 0);
    VirtMsgUnPacker unpacker(resp.payload_);
    BaseResponse acResp;
    acResp.Deserialize(unpacker);
    EXPECT_EQ(acResp.ret_, VirtIPCCode::UBSE_ERROR);
    MOCKER(&UrmaUtility::ResetBandWidth).reset();
}
} // namespace ovs::ut