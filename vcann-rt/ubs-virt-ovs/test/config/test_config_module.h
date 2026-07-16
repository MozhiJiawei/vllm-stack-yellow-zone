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
#ifndef UBSVIRTOVS_TEST_CONFIG_MODULE_H
#define UBSVIRTOVS_TEST_CONFIG_MODULE_H

#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"

namespace ovs::ut {
class TestConfigModule : public testing::Test {
public:
    TestConfigModule() = default;
    void SetUp() override;
    void TearDown() override;
};
} // namespace ovs::ut

#endif
