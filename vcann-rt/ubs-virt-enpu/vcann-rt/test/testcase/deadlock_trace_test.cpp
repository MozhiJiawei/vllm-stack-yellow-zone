/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
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

TEST_F(DeadlockTraceTest, copies_kernel_registration_names_and_handle)
{
    void *handle = reinterpret_cast<void *>(0x1111);
    const void *stub = reinterpret_cast<const void *>(0x2222);
    const void *device_function = reinterpret_cast<const void *>(0x3333);
    char name[] = "flash_attention_bfloat16_t_1_mix_aic";

    vcann_trace_kernel_register(handle, stub, name, device_function, 7);
    name[0] = 'X';

    ASSERT_EQ(g_vcann_kernel_registry.next_sequence, 1U);
    const vcann_kernel_registration_t &entry = g_vcann_kernel_registry.entries[0];
    EXPECT_EQ(entry.committed_sequence, 1U);
    EXPECT_EQ(entry.handle, reinterpret_cast<uintptr_t>(handle));
    EXPECT_EQ(entry.stub, reinterpret_cast<uintptr_t>(stub));
    EXPECT_EQ(entry.device_function, reinterpret_cast<uintptr_t>(device_function));
    EXPECT_EQ(entry.function_mode, 7U);
    EXPECT_STREQ(entry.stub_name, "flash_attention_bfloat16_t_1_mix_aic");
    EXPECT_STREQ(entry.device_name, "flash_attention_bfloat16_t_1_mix_aic");
    ASSERT_EQ(g_vcann_trace.next_sequence, 1U);
    EXPECT_EQ(g_vcann_trace.records[0].kind,
              static_cast<uint32_t>(VCANN_TRACE_KERNEL_REGISTER));
}

TEST_F(DeadlockTraceTest, host_sync_probe_stays_active_until_call_returns)
{
    rtStream_t stream = reinterpret_cast<rtStream_t>(0x1234);
    vcann_trace_host_sync_begin(VCANN_TRACE_STREAM_SYNC_BEGIN, stream, 55);
    EXPECT_EQ(g_vcann_host_sync_probe.active, 1U);
    EXPECT_EQ(g_vcann_host_sync_probe.kind,
              static_cast<uint32_t>(VCANN_TRACE_STREAM_SYNC_BEGIN));
    EXPECT_EQ(g_vcann_host_sync_probe.stream, reinterpret_cast<uintptr_t>(stream));
    EXPECT_EQ(g_vcann_host_sync_probe.timeout, 55);
    vcann_trace_host_sync_end(VCANN_TRACE_STREAM_SYNC_END, stream, RT_ERROR_NONE);
    EXPECT_EQ(g_vcann_host_sync_probe.active, 0U);
    EXPECT_EQ(g_vcann_trace.records[1].kind,
              static_cast<uint32_t>(VCANN_TRACE_STREAM_SYNC_END));
}

TEST_F(DeadlockTraceTest, unregister_removes_stale_handle_mapping)
{
    void *handle = reinterpret_cast<void *>(0x1111);
    vcann_trace_kernel_register(handle, nullptr, "matmul_bfloat16_t", nullptr, 0);

    vcann_trace_kernel_unregister(handle);

    EXPECT_EQ(g_vcann_kernel_registry.entries[0].handle, 0U);
    ASSERT_EQ(g_vcann_trace.next_sequence, 2U);
    EXPECT_EQ(g_vcann_trace.records[1].kind,
              static_cast<uint32_t>(VCANN_TRACE_KERNEL_UNREGISTER));
}

TEST_F(DeadlockTraceTest, acl_handle_mapping_is_deduplicated_and_removed_with_binary)
{
    void *binary = reinterpret_cast<void *>(0x1111);
    void *handle = reinterpret_cast<void *>(0x2222);
    vcann_trace_kernel_map_handle(handle, binary, "flash_attention_bfloat16");
    vcann_trace_kernel_map_handle(handle, binary, "flash_attention_bfloat16");

    ASSERT_EQ(g_vcann_kernel_registry.next_sequence, 1U);
    EXPECT_EQ(g_vcann_kernel_registry.entries[0].handle,
              reinterpret_cast<uintptr_t>(handle));
    EXPECT_EQ(g_vcann_kernel_registry.entries[0].stub,
              reinterpret_cast<uintptr_t>(binary));

    vcann_trace_kernel_unregister(binary);

    EXPECT_EQ(g_vcann_kernel_registry.entries[0].handle, 0U);

    vcann_trace_kernel_map_handle(handle, binary, "flash_attention_reloaded");
    ASSERT_EQ(g_vcann_kernel_registry.next_sequence, 1U);
    EXPECT_EQ(g_vcann_kernel_registry.entries[0].handle,
              reinterpret_cast<uintptr_t>(handle));
    EXPECT_STREQ(g_vcann_kernel_registry.entries[0].stub_name,
                 "flash_attention_reloaded");
}

TEST_F(DeadlockTraceTest, acl_handle_mapping_deduplicates_long_names)
{
    std::string name(VCANN_KERNEL_NAME_CAPACITY + 32, 'x');
    void *binary = reinterpret_cast<void *>(0x1111);
    void *handle = reinterpret_cast<void *>(0x2222);
    vcann_trace_kernel_map_handle(handle, binary, name.c_str());
    vcann_trace_kernel_map_handle(handle, binary, name.c_str());

    EXPECT_EQ(g_vcann_kernel_registry.next_sequence, 1U);
}

TEST_F(DeadlockTraceTest, concurrent_acl_handle_mapping_uses_one_slot)
{
    constexpr uint32_t thread_count = 8;
    std::vector<std::thread> writers;
    for (uint32_t index = 0; index < thread_count; ++index) {
        writers.emplace_back([] {
            vcann_trace_kernel_map_handle(reinterpret_cast<void *>(0x2222),
                                          reinterpret_cast<void *>(0x1111),
                                          "flash_attention_bfloat16");
        });
    }
    for (std::thread &writer : writers) {
        writer.join();
    }

    EXPECT_EQ(g_vcann_kernel_registry.next_sequence, 1U);
}

TEST_F(DeadlockTraceTest, registry_overflow_is_bounded)
{
    for (uint32_t index = 0; index < VCANN_KERNEL_REGISTRY_CAPACITY + 3; ++index) {
        vcann_trace_kernel_register(reinterpret_cast<void *>(static_cast<uintptr_t>(index + 1)),
                                    nullptr, "kernel", nullptr, 0);
    }
    EXPECT_EQ(g_vcann_kernel_registry.next_sequence, VCANN_KERNEL_REGISTRY_CAPACITY);
    EXPECT_EQ(g_vcann_kernel_registry.dropped, 3U);
}

TEST_F(DeadlockTraceTest, concurrent_kernel_registration_commits_every_slot)
{
    constexpr uint32_t thread_count = 4;
    constexpr uint32_t registrations_per_thread = VCANN_KERNEL_REGISTRY_CAPACITY / thread_count;
    std::vector<std::thread> writers;
    for (uint32_t thread = 0; thread < thread_count; ++thread) {
        writers.emplace_back([thread] {
            for (uint32_t index = 0; index < registrations_per_thread; ++index) {
                uintptr_t handle = 1 + thread * registrations_per_thread + index;
                vcann_trace_kernel_register(reinterpret_cast<void *>(handle), nullptr,
                                            "kernel", nullptr, 0);
            }
        });
    }
    for (std::thread &writer : writers) {
        writer.join();
    }

    ASSERT_EQ(g_vcann_kernel_registry.next_sequence, VCANN_KERNEL_REGISTRY_CAPACITY);
    EXPECT_EQ(g_vcann_kernel_registry.dropped, 0U);
    for (uint32_t index = 0; index < VCANN_KERNEL_REGISTRY_CAPACITY; ++index) {
        EXPECT_EQ(g_vcann_kernel_registry.entries[index].committed_sequence, index + 1);
    }
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
