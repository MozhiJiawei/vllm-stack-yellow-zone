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

#include "test_auth_manager.h"

#include "config_module.h"

using namespace virt::ovs::ipc::server;
namespace ovs::ut {

TEST_F(TestAuthManager, AuthorizeService_SpacesInAuthorityItems)
{
    EXPECT_TRUE(AuthManager::AuthorizeService(" a , b , c ", "a"));
    EXPECT_TRUE(AuthManager::AuthorizeService(" a , b , c ", "b"));
    EXPECT_TRUE(AuthManager::AuthorizeService(" a , b , c ", "c"));
}

TEST_F(TestAuthManager, AuthorizeService_SpacesInServiceKey)
{
    EXPECT_TRUE(AuthManager::AuthorizeService("a, b, c", " a "));
    EXPECT_TRUE(AuthManager::AuthorizeService("a, b, c", " b "));
}

TEST_F(TestAuthManager, AuthorizeService_EmptyServiceKey)
{
    EXPECT_FALSE(AuthManager::AuthorizeService("a, b, c", ""));
}

TEST_F(TestAuthManager, AuthorizeService_OnlySpacesServiceKey)
{
    EXPECT_FALSE(AuthManager::AuthorizeService("a, b, c", "   "));
}

TEST_F(TestAuthManager, AuthorizeService_SingleItem)
{
    EXPECT_TRUE(AuthManager::AuthorizeService("only", "only"));
    EXPECT_FALSE(AuthManager::AuthorizeService("only", "other"));
}

TEST_F(TestAuthManager, AuthorizeService_FirstItemNoMatch)
{
    EXPECT_TRUE(AuthManager::AuthorizeService("x, target", "target"));
    EXPECT_FALSE(AuthManager::AuthorizeService("x, y", "target"));
}

TEST_F(TestAuthManager, AuthorizeService_TrailingComma)
{
    EXPECT_TRUE(AuthManager::AuthorizeService("a,b,", "a"));
    EXPECT_FALSE(AuthManager::AuthorizeService("a,b,", ""));
}

} // namespace ovs::ut
