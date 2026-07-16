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

#ifndef VIRT_OVS_IPC_SERVER_THREAD_POOL_H
#define VIRT_OVS_IPC_SERVER_THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>
#include <vector>

#include "common/constants.h"

namespace virt::ovs::ipc::server {

class ThreadPool {
public:
    explicit ThreadPool(size_t n);
    ~ThreadPool();

    void Start();
    void Stop();

    bool TryEnqueue(std::function<void()> task);
    size_t QueueSize() const;

private:
    void Worker();

    size_t maxQueueSize_{constants::THREAD_POOL_DEFAULT_QUEUE_SIZE};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::condition_variable cv_;
};

} // namespace virt::ovs::ipc::server

#endif