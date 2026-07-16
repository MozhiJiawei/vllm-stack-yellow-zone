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

#ifndef TEST_URMA_SERVICE_H
#define TEST_URMA_SERVICE_H

#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#include "urma_service.h"
namespace ovs::ut {

using namespace virt::ovs::service::urma;

const uint32_t minBandwidth = 8;
const uint32_t maxBandwidth = 16;

class TestUrmaService : public testing::Test {
public:
    TestUrmaService() = default;
    void SetUp() override;
    void TearDown() override;

protected:
    std::shared_ptr<UrmaService> service;
};
} // namespace ovs::ut
#endif // TEST_URMA_SERVICE_H