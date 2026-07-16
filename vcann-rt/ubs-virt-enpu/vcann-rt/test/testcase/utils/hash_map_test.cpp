/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "hash_map.h"
#include <acl/acl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <atomic>
#include <mockcpp/mockcpp.hpp>
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"

using namespace testing;

class HashMapTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Hash map test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Hash map test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../build/log/enpu/");
        open(stub_lock_path(), O_CREAT | O_RDONLY, 777); // ut memctl.lock,777
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
    }

    void TearDown()
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }
};

TEST_F(HashMapTest, hashmap_createTest_success)
{
    size_t cap = 10;
    HashMap *ret = hashmap_create(cap);
    EXPECT_NE(ret, nullptr);
}

TEST_F(HashMapTest, hashmap_createTest_capacity_zero)
{
    size_t cap = 0;
    HashMap *ret = hashmap_create(cap);
    EXPECT_EQ(ret, nullptr);
}

TEST_F(HashMapTest, hashmap_put_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(map->size, 1);
    void *ptr1 = reinterpret_cast<void *>(120);
    ret = hashmap_put(map, key, ptr1, false);
}

TEST_F(HashMapTest, hashmap_put_old_node)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(map->size, 1);
    void *ptr1 = reinterpret_cast<void *>(120);
    void *ret_ptr = nullptr;
    ret = hashmap_put(map, key, ptr1, false);
    EXPECT_EQ(map->size, 1);
    ret = hashmap_get_ptr(map, key, &ret_ptr);
    EXPECT_EQ(ret_ptr, ptr1);
}

TEST_F(HashMapTest, hashmap_put_key_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = nullptr;
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(ret, -1);
}

TEST_F(HashMapTest, hashmap_get_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    MapValue value;
    ret = hashmap_get(map, key, &value);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(value.ptr, ptr);
}

TEST_F(HashMapTest, hashmap_get_key_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    MapValue value;
    key = nullptr;
    ret = hashmap_get(map, key, &value);
    EXPECT_EQ(ret, -1);
}

TEST_F(HashMapTest, hashmap_get_ptr_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    void *ret_ptr = nullptr;
    ret = hashmap_get_ptr(map, key, &ret_ptr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ret_ptr, ptr);
}

TEST_F(HashMapTest, hashmap_get_ptr_key_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    void *ret_ptr = nullptr;
    key = nullptr;
    ret = hashmap_get_ptr(map, key, &ret_ptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(HashMapTest, hashmap_get_capture_status_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    bool capture_status;
    ret = hashmap_get_capture_status(map, key, &capture_status);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(capture_status, true);
}

TEST_F(HashMapTest, hashmap_get_capture_status_key_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    bool capture_status;
    key = nullptr;
    ret = hashmap_get_capture_status(map, key, &capture_status);
    EXPECT_EQ(ret, -1);
}

TEST_F(HashMapTest, hashmap_remove_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    ret = hashmap_remove(map, key);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(map->size, 0);
}

TEST_F(HashMapTest, hashmap_remove_key_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    key = nullptr;
    ret = hashmap_remove(map, key);
    EXPECT_EQ(ret, -1);
}

TEST_F(HashMapTest, hashmap_contains_success)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    ret = hashmap_contains(map, key);
    EXPECT_EQ(ret, 1);
}

TEST_F(HashMapTest, hashmap_contains_map_nullptr)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(map->size, 1);
    key = nullptr;
    ret = hashmap_contains(map, key);
    EXPECT_EQ(ret, 0);
}

TEST_F(HashMapTest, hashmap_size_test)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(ret, 0);
    ret = hashmap_size(map);
    EXPECT_EQ(ret, 1);
}

TEST_F(HashMapTest, hashmap_destroy_test)
{
    size_t cap = 10;
    HashMap *map = hashmap_create(cap);
    EXPECT_NE(map, nullptr);
    void *key = reinterpret_cast<void *>(100);
    void *ptr = reinterpret_cast<void *>(110);
    int ret = hashmap_put(map, key, ptr, true);
    EXPECT_EQ(ret, 0);
    hashmap_destroy(map);
}