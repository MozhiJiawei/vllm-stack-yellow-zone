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

#include "mem_limiter.h"
#include <acl/acl.h>
#include <errno.h>
#include <runtime/rt.h>
#include <string.h>
#include <sys/stat.h>
#include "runtime_hook.h"

bool memory_check(size_t requested)
{
    size_t used = 0;
    int ret = get_mem_used(&used);
    CHECK_COND_RETURN_(ret != 0, false, "get mem used failed.");
    size_t quota = get_mem_limit_quota();
    size_t total;
    bool result = __builtin_add_overflow(requested, used, &total);
    CHECK_COND_RETURN_(result, false, "User requested mem size too big! Request:%zu B, used:%zu B, quota:%zu B.",
                       requested, used, quota);
    CHECK_COND_RETURN_((total > quota), false, "Out of memory! Request:%zu B, used:%zu B, quota:%zu B.", requested,
                       used, quota);
    return true;
}

int guard_memory(size_t requested)
{
    return ENPU_SUCCESS;
    file_lock lock = file_lock_create(lock_path(), LOCK_EX);
    if (!file_lock_isvalid(&lock)) {
        LOG_ERROR("Guard memory failed to create file lock!");
        file_lock_destroy(&lock);
        return ACL_ERROR_FAILURE;
    }

    if (!memory_check(requested)) {
        LOG_ERROR("Guard memory out of memory error for requested:%zd", requested);
        file_lock_destroy(&lock);
        return ACL_ERROR_STORAGE_OVER_LIMIT;
    }
    file_lock_destroy(&lock);
    return ENPU_SUCCESS;
}

const char *lock_path()
{
    return MEMCTL_LOCK_PATH;
}

int create_file_lock_base_dir()
{
    char cmd[128];
    int ans = snprintf_s(cmd, sizeof(cmd), sizeof(cmd), "mkdir -p -m 750 %s", FILE_LOCK_BASE_DIR);
    CHECK_COND_RETURN_ERROR_CODE(ans < 0, "Can not concatenate string to create dir.");
    int ret = system(cmd);
    CHECK_COND_RETURN_ERROR_CODE(ret < 0 && errno != EEXIST, "create %s failed, err is %s.", FILE_LOCK_BASE_DIR,
                                 strerror(errno));

    LOG_INFO("create %s success", FILE_LOCK_BASE_DIR);
    return ENPU_SUCCESS;
}

int memory_limiter_init()
{
    return create_file_lock_base_dir();
}
