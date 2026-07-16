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

#include <errno.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <stdint.h>
#include <sys/file.h>
#include <unistd.h>
#include <mockcpp/mockcpp.hpp>

#include "acl/acl.h"
#include "common.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"
#include "utils.h"

extern "C" {
static size_t g_mock_mem_used_value = 0;
static int g_mock_mem_used_return = ENPU_SUCCESS;
static int g_mock_mem_used_errno = 0;

int stub_get_mem_used_setter(size_t *used)
{
    *used = g_mock_mem_used_value;
    if (g_mock_mem_used_errno != 0) {
        errno = g_mock_mem_used_errno;
    }
    return g_mock_mem_used_return;
}

file_lock stub_file_lock_invalid(const char *path, int operation)
{
    (void)path;
    (void)operation;
    file_lock lock;
    lock.fd = -1;
    lock.held = false;
    return lock;
}
}

// C++ overload required by mockcpp's IsEqual<file_lock> template when
// MOCKER(file_lock_create) is used. file_lock is a plain C struct and
// has no built-in operator==.
inline bool operator==(const file_lock &a, const file_lock &b)
{
    return a.fd == b.fd && a.held == b.held;
}

class MemLimiterTest : public testing::Test {
protected:
    void SetUp() override
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755);
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
        g_mock_mem_used_value = 512 * MB_TO_B;
        g_mock_mem_used_return = ENPU_SUCCESS;
        g_mock_mem_used_errno = 0;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        GlobalMockObject::reset();
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

// create_file_lock_base_dir succeeds when system() returns 0.
TEST_F(MemLimiterTest, CreateFileLockBaseDirSuccess)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(0));
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_SUCCESS);
}

// create_file_lock_base_dir is idempotent: second call also succeeds.
TEST_F(MemLimiterTest, CreateFileLockBaseDirIdempotent)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(0));
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_SUCCESS);
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_SUCCESS);
}

// create_file_lock_base_dir fails when system() returns -1 and errno != EEXIST.
TEST_F(MemLimiterTest, CreateFileLockBaseDirFailure)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(-1));
    errno = EACCES;
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_FAIL);
}

// create_file_lock_base_dir treats EEXIST as benign even when system() returns -1.
TEST_F(MemLimiterTest, CreateFileLockBaseDirRetFailErrnoEexist)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(-1));
    errno = EEXIST;
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_SUCCESS);
}

// memory_limiter_init returns ENPU_SUCCESS on mkdir success.
TEST_F(MemLimiterTest, MemoryLimiterInitSuccess)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(0));
    EXPECT_EQ(memory_limiter_init(), ENPU_SUCCESS);
}

// memory_limiter_init propagates create_file_lock_base_dir failure.
TEST_F(MemLimiterTest, MemoryLimiterInitPropagatesFailure)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(-1));
    errno = EACCES;
    EXPECT_EQ(memory_limiter_init(), ENPU_FAIL);
}

// memory_check returns true when allocation is small and well under quota.
TEST_F(MemLimiterTest, MemoryCheckUnderQuota)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 512 * MB_TO_B;
    EXPECT_TRUE(memory_check(1024));
}

// memory_check returns true at the boundary: used + 0 == quota.
TEST_F(MemLimiterTest, MemoryCheckAtQuotaBoundary)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = get_mem_limit_quota();
    EXPECT_TRUE(memory_check(0));
}

// memory_check returns false when one byte past quota.
TEST_F(MemLimiterTest, MemoryCheckOverQuotaByOne)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = get_mem_limit_quota();
    EXPECT_FALSE(memory_check(1));
}

// memory_check detects __builtin_add_overflow when used + requested overflows size_t.
TEST_F(MemLimiterTest, MemoryCheckOverflow)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = SIZE_MAX / 2;
    EXPECT_FALSE(memory_check(SIZE_MAX));
}

// memory_check returns false when get_mem_used returns -1.
TEST_F(MemLimiterTest, MemoryCheckDcmiFails)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_return = -1;
    g_mock_mem_used_errno = EIO;
    EXPECT_FALSE(memory_check(1024));
}

// guard_memory returns ENPU_SUCCESS for a small allocation under quota.
TEST_F(MemLimiterTest, GuardMemorySuccess)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 512 * MB_TO_B;
    EXPECT_EQ(guard_memory(1024), ENPU_SUCCESS);
}

// guard_memory accepts zero-size allocation even if already at quota.
TEST_F(MemLimiterTest, GuardMemoryZeroSize)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = get_mem_limit_quota();
    EXPECT_EQ(guard_memory(0), ENPU_SUCCESS);
}

// guard_memory returns ACL_ERROR_STORAGE_OVER_LIMIT when one byte over quota.
TEST_F(MemLimiterTest, GuardMemoryOverQuota)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = get_mem_limit_quota();
    EXPECT_EQ(guard_memory(1), ACL_ERROR_STORAGE_OVER_LIMIT);
}

// guard_memory returns ACL_ERROR_FAILURE when file lock cannot be created.
TEST_F(MemLimiterTest, GuardMemoryLockFails)
{
    MOCKER(file_lock_create).stubs().will(invoke(stub_file_lock_invalid));
    EXPECT_EQ(guard_memory(1024), ACL_ERROR_FAILURE);
}

// guard_memory returns ACL_ERROR_STORAGE_OVER_LIMIT when DCMI fails.
TEST_F(MemLimiterTest, GuardMemoryDcmiFails)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 0;
    g_mock_mem_used_return = -1;
    g_mock_mem_used_errno = EIO;
    EXPECT_EQ(guard_memory(1024), ACL_ERROR_STORAGE_OVER_LIMIT);
}

// memory_check returns true when both used and requested are zero.
TEST_F(MemLimiterTest, MemoryCheckZeroZero)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 0;
    EXPECT_TRUE(memory_check(0));
}

// memory_check returns true for a typical large allocation under quota.
TEST_F(MemLimiterTest, MemoryCheckLargeAllocationUnderQuota)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 0;
    size_t large = (size_t)1024 * 1024 * 1024;
    EXPECT_TRUE(memory_check(large));
}

// guard_memory returns ENPU_SUCCESS at the exact boundary (used + requested == quota).
TEST_F(MemLimiterTest, GuardMemoryAtBoundary)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    size_t half = get_mem_limit_quota() / 2;
    g_mock_mem_used_value = half;
    EXPECT_EQ(guard_memory(half), ENPU_SUCCESS);
}

// guard_memory releases the file lock after returning ACL_ERROR_STORAGE_OVER_LIMIT.
TEST_F(MemLimiterTest, GuardMemoryReleasesLockOnFailure)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = get_mem_limit_quota();
    EXPECT_EQ(guard_memory(1), ACL_ERROR_STORAGE_OVER_LIMIT);
    // If the lock was leaked, LOCK_NB would fail with EWOULDBLOCK.
    file_lock probe = file_lock_create(stub_lock_path(), LOCK_EX | LOCK_NB);
    EXPECT_TRUE(file_lock_isvalid(&probe));
    file_lock_destroy(&probe);
}

// guard_memory releases the file lock after the success path.
TEST_F(MemLimiterTest, GuardMemoryReleasesLockOnSuccess)
{
    MOCKER(get_mem_used, int(size_t *)).stubs().will(invoke(stub_get_mem_used_setter));
    g_mock_mem_used_value = 0;
    EXPECT_EQ(guard_memory(1024), ENPU_SUCCESS);
    file_lock probe = file_lock_create(stub_lock_path(), LOCK_EX | LOCK_NB);
    EXPECT_TRUE(file_lock_isvalid(&probe));
    file_lock_destroy(&probe);
}

// create_file_lock_base_dir returns ENPU_SUCCESS even if a previous call set errno.
TEST_F(MemLimiterTest, CreateFileLockBaseDirResetsErrno)
{
    MOCKER(system, int(const char *)).stubs().will(returnValue(0));
    errno = EIO;
    EXPECT_EQ(create_file_lock_base_dir(), ENPU_SUCCESS);
}
