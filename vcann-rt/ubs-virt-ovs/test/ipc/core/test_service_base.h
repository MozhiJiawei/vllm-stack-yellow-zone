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

#ifndef TEST_SERVICE_BASE_H
#define TEST_SERVICE_BASE_H

#include "gtest/gtest.h"

#include "service_base.h"
#include "virt_msg.h"

namespace ovs::ut {

using namespace virt::ovs;
using namespace virt::ovs::msg;

class TestServiceBase : public Service {
public:
    TestServiceBase() = default;

    std::string Name() const override;

    IpcResponse FoolHandler(const std::string &req);

    std::string lastPayload;
};
} // namespace ovs::ut
#endif // TEST_SERVICE_BASE_H