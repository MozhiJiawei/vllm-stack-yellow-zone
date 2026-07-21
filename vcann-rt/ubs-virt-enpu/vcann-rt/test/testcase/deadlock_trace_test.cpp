/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <cstdlib>
#include <thread>
#include <vector>
#include "deadlock_trace.h"

class DeadlockTraceTest : public testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_EQ(setenv("ENPU_DEADLOCK_TRACE", "1", 1), 0);
        vcann_trace_init();
    }

    void TearDown() override
    {
        unsetenv("ENPU_DEADLOCK_TRACE");
        vcann_trace_init();
    }
};

TEST_F(DeadlockTraceTest, records_committed_kernel_metadata)
{
    rtStream_t stream = reinterpret_cast<rtStream_t>(0x1234);
    const void *kernel = reinterpret_cast<const void *>(0x5678);
    const void *args = reinterpret_cast<const void *>(0x9abc);
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_LAUNCH, stream, kernel, args, 17, 8, 64);

    ASSERT_EQ(g_vcann_trace.next_sequence, 1U);
    const vcann_trace_record_t &record = g_vcann_trace.records[0];
    EXPECT_EQ(record.committed_sequence, 1U);
    EXPECT_EQ(record.kind, static_cast<uint32_t>(VCANN_TRACE_RT_KERNEL_LAUNCH));
    EXPECT_EQ(record.stream, reinterpret_cast<uintptr_t>(stream));
    EXPECT_EQ(record.object, reinterpret_cast<uintptr_t>(kernel));
    EXPECT_EQ(record.auxiliary, reinterpret_cast<uintptr_t>(args));
    EXPECT_EQ(record.value, 17U);
    EXPECT_EQ(record.blocks, 8U);
    EXPECT_EQ(record.args_size, 64U);
    EXPECT_NE(record.timestamp_ns, 0U);
    EXPECT_NE(record.tid, 0U);
}

TEST_F(DeadlockTraceTest, sync_probe_stays_active_until_sync_returns)
{
    rtStream_t stream = reinterpret_cast<rtStream_t>(0x1234);
    vcann_trace_sync_begin(stream, 1, 42, 0);
    EXPECT_EQ(g_vcann_sync_probe.active, 1U);
    EXPECT_EQ(g_vcann_sync_probe.stream, reinterpret_cast<uintptr_t>(stream));
    EXPECT_EQ(g_vcann_sync_probe.owner, 1);
    EXPECT_EQ(g_vcann_sync_probe.schedule_turn, 42U);
    vcann_trace_sync_end(stream);
    EXPECT_EQ(g_vcann_sync_probe.active, 0U);
    EXPECT_EQ(g_vcann_trace.records[1].kind, static_cast<uint32_t>(VCANN_TRACE_SCHED_SYNC_END));
}

TEST(DeadlockTraceDisabledTest, disabled_trace_does_not_advance_ring)
{
    unsetenv("ENPU_DEADLOCK_TRACE");
    vcann_trace_init();
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_LAUNCH, nullptr, nullptr, nullptr, 0, 0, 0);
    EXPECT_EQ(g_vcann_trace.enabled, 0U);
    EXPECT_EQ(g_vcann_trace.next_sequence, 0U);
}

TEST_F(DeadlockTraceTest, concurrent_wrap_keeps_newest_ring_generation)
{
    constexpr int thread_count = 4;
    std::vector<std::thread> writers;
    for (int thread = 0; thread < thread_count; ++thread) {
        writers.emplace_back([] {
            for (uint32_t index = 0; index < VCANN_TRACE_CAPACITY; ++index) {
                vcann_trace_record(VCANN_TRACE_RT_KERNEL_LAUNCH, nullptr, nullptr, nullptr, index, 0, 0);
            }
        });
    }
    for (std::thread &writer : writers) {
        writer.join();
    }

    ASSERT_EQ(g_vcann_trace.next_sequence, thread_count * VCANN_TRACE_CAPACITY);
    const uint64_t first_expected =
        (thread_count - 1) * static_cast<uint64_t>(VCANN_TRACE_CAPACITY) + 1;
    for (uint32_t slot = 0; slot < VCANN_TRACE_CAPACITY; ++slot) {
        EXPECT_EQ(g_vcann_trace.records[slot].committed_sequence, first_expected + slot);
    }
}
