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
#include "../include/utils.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../include/common.h"

void *map_share_mem(const char *shmID, size_t size)
{
    if (shmID == NULL || size == 0) {
        LOG_ERROR("shmID or size is NULL");
        return NULL;
    }
    int fd = shm_open(shmID, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    CHECK_COND_RETURN_(fd == -1, NULL, "Failed to shm_open, fd = %d.", fd);

    struct stat st;
    if (fstat(fd, &st) != 0) {
        LOG_ERROR("Failed to fstat");
        close(fd);
        return NULL;
    }
    if (st.st_size >= 0 && (size_t)st.st_size < size) {
        if (ftruncate(fd, size) == -1) {
            LOG_ERROR("Failed to ftruncate");
            close(fd);
            return NULL;
        }
    }

    void *addr_ = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr_ == MAP_FAILED) {
        LOG_ERROR("Failed to get shared memory");
        return NULL;
    }

    return addr_;
}

static bool file_lock_acquire(file_lock *lock, int operation)
{
    if (!lock) {
        LOG_ERROR("file_lock_acquire: lock is NULL");
        return false;
    }

    if (lock->fd == -1) {
        LOG_ERROR("file_lock_acquire: invalid file descriptor");
        return false;
    }

    if (lock->held) {
        LOG_ERROR("file_lock_acquire: lock already held");
        return false;
    }

    int ret = flock(lock->fd, operation);
    if (ret != 0) {
        LOG_ERROR("lock failed, fd %d, errno %s", lock->fd, strerror(errno));
        return false;
    }

    lock->held = true;
    return true;
}

file_lock file_lock_create(const char *path, int operation)
{
    file_lock lock = {-1, false};

    if (!path) {
        LOG_ERROR("file_lock_create: path is NULL");
        return lock;
    }

    lock.fd = open(path, O_CREAT | O_RDONLY, 0);
    if (lock.fd == -1) {
        LOG_ERROR("file_lock_create: open file failed, path: %s", path);
        return lock;
    }

    if (operation != 0) {
        lock.held = file_lock_acquire(&lock, operation);
    }

    return lock;
}

static bool file_lock_release(file_lock *lock)
{
    if (!lock) {
        LOG_ERROR("file_lock_release: lock is NULL");
        return false;
    }

    if (!lock->held) {
        LOG_ERROR("file_lock_release: lock not held");
        return false;
    }

    int ret = flock(lock->fd, LOCK_UN);
    CHECK_COND_RETURN_(ret != 0, false, "unlock failed, fd %d, errno %s.", lock->fd, strerror(errno));

    lock->held = false;
    return true;
}

void file_lock_destroy(file_lock *lock)
{
    if (!lock) {
        return;
    }

    if (lock->fd == -1) {
        return;
    }

    if (lock->held) {
        file_lock_release(lock);
    }

    if (close(lock->fd) == -1) {
        LOG_ERROR("close file failed, fd %d, errno is %s", lock->fd, strerror(errno));
    }

    lock->fd = -1;
    lock->held = false;
}
