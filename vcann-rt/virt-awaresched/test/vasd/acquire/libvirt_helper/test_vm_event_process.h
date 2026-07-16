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
#ifndef TEST_VM_EVENT_PROCESS_H
#define TEST_VM_EVENT_PROCESS_H

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

namespace vas::ut::acquire {
class TestVmEventProcess : public testing::Test {
protected:
    TestVmEventProcess() = default;

    void SetUp() override;

    void TearDown() override;
};
} // namespace vas::ut::acquire

#endif // TEST_VM_EVENT_PROCESS_H