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
#include "thread_pool.h"
#include "logger.h"

namespace virt::ovs::ipc::server {
ThreadPool::ThreadPool(size_t n)
{
    workers_.reserve(n);
}

ThreadPool::~ThreadPool()
{
    Stop();
}

void ThreadPool::Start()
{
    LOG_INFO << "ThreadPool starting, workers=" << workers_.capacity();
    running_ = true;
    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back(&ThreadPool::Worker, this);
    }
}

void ThreadPool::Stop()
{
    LOG_INFO << "ThreadPool stopping";
    running_ = false;
    cv_.notify_all();
    for (auto &t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    LOG_INFO << "ThreadPool stopped";
}

bool ThreadPool::TryEnqueue(std::function<void()> task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.size() >= maxQueueSize_) {
        return false;
    }
    tasks_.push(std::move(task));
    cv_.notify_one();
    return true;
}

size_t ThreadPool::QueueSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::Worker()
{
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !tasks_.empty() || !running_; });
            if (!running_) {
                break;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}
} // namespace virt::ovs::ipc::server