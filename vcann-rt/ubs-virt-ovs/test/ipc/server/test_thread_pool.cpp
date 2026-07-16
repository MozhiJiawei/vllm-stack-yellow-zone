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

#include "test_thread_pool.h"

#include "common/constants.h"

using namespace virt::ovs::ipc::server;
using namespace virt::ovs;
namespace ovs::ut {

TEST_F(TestThreadPool, StartAndStop)
{
    ThreadPool pool(2); // thread num is 2
    EXPECT_NO_THROW({
        pool.Start();
        pool.Stop();
    });
}

TEST_F(TestThreadPool, EnqueueTaskExecuted)
{
    ThreadPool pool(1);
    pool.Start();

    std::atomic<bool> executed{false};
    std::mutex mtx;
    std::condition_variable cv;

    bool ok = pool.TryEnqueue([&] {
        executed = true;
        cv.notify_one();
    });

    EXPECT_TRUE(ok);
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(1), [&] { return executed.load(); });
    }

    EXPECT_TRUE(executed);
    pool.Stop();
}

TEST_F(TestThreadPool, QueueSizeCoverage)
{
    ThreadPool pool(1);

    EXPECT_EQ(pool.QueueSize(), 0);

    pool.Start();
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> done{false};

    pool.TryEnqueue([&] {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return done.load(); });
    });

    EXPECT_GT(pool.QueueSize(), 0u);
    done = true;
    cv.notify_all();
    pool.Stop();
}

TEST_F(TestThreadPool, TryEnqueue_QueueFull)
{
    ThreadPool pool(1);
    pool.Start();

    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    std::atomic<int> executed{0};

    pool.TryEnqueue([&] {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return done.load(); });
        executed++;
    });

    for (int i = 0; i < constants::THREAD_POOL_DEFAULT_QUEUE_SIZE + 1; ++i) {
        pool.TryEnqueue([&] { executed++; });
    }

    done = true;
    cv.notify_all();
    pool.Stop();
}

TEST_F(TestThreadPool, DoubleStop)
{
    ThreadPool pool(1);
    pool.Start();
    pool.Stop();
    EXPECT_NO_THROW(pool.Stop());
}
} // namespace ovs::ut