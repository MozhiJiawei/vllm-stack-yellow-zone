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
#ifndef __UTILS_H__
#define __UTILS_H__
#include <sys/file.h>
#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_SIZE 1024

typedef struct file_lock {
    int fd;
    bool held;
} file_lock;

typedef struct {
    int key;
    int value;
    int used;
} key_value_pair;

file_lock file_lock_create(const char *path, int operation);
extern void file_lock_destroy(file_lock *lock);

static inline bool file_lock_held(const file_lock *lock)
{
    return lock && lock->held;
}

static inline bool file_lock_isvalid(const file_lock *lock)
{
    return lock && lock->fd >= 0;
}

extern void *map_share_mem(const char *shmID, size_t size);

#if defined(__cplusplus)
}
#endif

#endif