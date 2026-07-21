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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <runtime/rt.h>
#include <sys/file.h>
#include <unistd.h>
#include <atomic>
#include <mockcpp/mockcpp.hpp>

#include "common.h"
#include "core_limiter.h"
#include "hash_map.h"
#include "log.h"
#include "mem_limiter.h"
#include "npu_manager.h"
#include "runtime_stub.h"
#include "securec.h"

extern "C" {
// Globals defined in core_limiter.c needed for state inspection.
extern cache_streams_t g_cache_streams;
extern HashMap *stream_map;
extern HashMap *event_map;
extern vnpu_time_slice_sched_t *g_vnpu_sched_context;
extern uint8_t g_vnpu_id;
extern std::atomic<bool> g_sched_locking;
extern pthread_mutex_t g_sched_mutex;

// Internal helpers used by the stream/event capture state machine.
bool is_vnpu_alive(int vnpu_id);
bool is_vnpu_in_prefill(int vnpu_id);
bool vnpu_borrow_allowed(int owner);
bool check_timeout(atomic_uint_fast64_t *timestamp, uint64_t timeout_period);
// restore_streams is external-linkage in core_limiter.c but not declared in any public header.
void restore_streams(rtStream_t stream);
}

class CoreLimiterTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        std::cout << "Core Limiter test start" << std::endl;
    }

    static void TearDownTestCase()
    {
        std::cout << "Core Limiter test end" << std::endl;
    }

    void SetUp()
    {
        (void)sprintf_s(g_log_config.log_dir, sizeof(g_log_config.log_dir), "%s", "../__build/log/enpu/");
        fd_ = open(stub_lock_path(), O_CREAT | O_RDONLY, 0755); // ut中的memctl.lock文件,设置为755权限
        MOCKER(lock_path).stubs().will(invoke(stub_lock_path));
        MOCKER(enpu_load_config).stubs().will(invoke(stub_enpu_load_config));
        enpu_global_init();
        MOCKER(load_rt_libraries).stubs().will(invoke(stub_load_rt_libraries));
    }
    void TearDown() override
    {
        g_cache_streams.num_streams = 0;
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }

private:
    int fd_ = -1;
};

TEST_F(CoreLimiterTest, npu_utilization_monitor_thread_test)
{
    void *para = nullptr;
    (void)npu_utilization_monitor_thread(para);
    int owner = 0;
    check_and_borrow_timeslice(owner);
    bool ret = slide_window_check(owner);
    EXPECT_EQ(ret, false);
    owner = get_vnpu_id();
    check_and_borrow_timeslice(owner);
    ret = slide_window_check(owner);
    EXPECT_EQ(ret, false);
}

TEST_F(CoreLimiterTest, calculate_alive_vnpu_num_test)
{
    int32_t count = calculate_alive_vnpu_num();
    EXPECT_EQ(count, 1);
}

// ns_now must be strictly increasing across two calls.
TEST_F(CoreLimiterTest, ns_now_monotonic)
{
    uint64_t t1 = ns_now();
    uint64_t t2 = ns_now();
    EXPECT_GE(t2, t1);
}

// add_stream inserts a new stream pointer into stream_map exactly once.
TEST_F(CoreLimiterTest, add_stream_basic)
{
    void *stm = reinterpret_cast<void *>(0x1001);
    g_cache_streams.num_streams = 0;
    add_stream(stm);
    EXPECT_EQ(hashmap_contains(stream_map, stm), 1);
}

// add_stream on an already-contained stream is a no-op (no duplicate entry).
TEST_F(CoreLimiterTest, add_stream_idempotent)
{
    void *stm = reinterpret_cast<void *>(0x1002);
    g_cache_streams.num_streams = 0;
    add_stream(stm);
    size_t size_after_first = hashmap_size(stream_map);
    add_stream(stm);
    EXPECT_EQ(hashmap_size(stream_map), size_after_first);
}

// remove_stream removes the stream from both g_cache_streams and stream_map.
TEST_F(CoreLimiterTest, remove_stream_basic)
{
    void *stm = reinterpret_cast<void *>(0x1003);
    g_cache_streams.num_streams = 0;
    restore_streams(stm);
    remove_stream(NULL, stm);
    EXPECT_EQ(hashmap_contains(stream_map, stm), 0);
}

// remove_stream on a non-cached stream is a silent no-op.
TEST_F(CoreLimiterTest, remove_stream_not_in_cache)
{
    void *stm = reinterpret_cast<void *>(0x1004);
    g_cache_streams.num_streams = 0;
    remove_stream(NULL, stm);
    EXPECT_EQ(hashmap_contains(stream_map, stm), 0);
}

// set_stream_capture(true) marks the stream as captured in stream_map.
TEST_F(CoreLimiterTest, set_stream_capture_true)
{
    void *stm = reinterpret_cast<void *>(0x2001);
    g_cache_streams.num_streams = 0;
    bool capture = true;
    set_stream_capture(&capture, stm);
    bool stored = false;
    EXPECT_EQ(hashmap_get_capture_status(stream_map, stm, &stored), 0);
    EXPECT_TRUE(stored);
    hashmap_remove(stream_map, stm);
}

// set_stream_capture(false) resets capture status on streams pointing at head.
TEST_F(CoreLimiterTest, set_stream_capture_false)
{
    void *head = reinterpret_cast<void *>(0x2002);
    void *other = reinterpret_cast<void *>(0x2003);
    g_cache_streams.num_streams = 0;
    restore_streams(head);
    restore_streams(other);
    // Mark other as part of head's capture chain (head ptr, capture=true).
    hashmap_put(stream_map, other, head, true);
    // Pre-condition: other is in capture mode before clearing.
    bool pre_stored = false;
    hashmap_get_capture_status(stream_map, other, &pre_stored);
    EXPECT_TRUE(pre_stored);
    bool capture = false;
    set_stream_capture(&capture, head);
    bool stored = false;
    hashmap_get_capture_status(stream_map, other, &stored);
    EXPECT_FALSE(stored);
}

// set_event_create_status inserts a new event into event_map with ptr=NULL.
TEST_F(CoreLimiterTest, set_event_create_status)
{
    void *evt = reinterpret_cast<void *>(0x3001);
    size_t size_before = hashmap_size(event_map);
    set_event_create_status(evt);
    EXPECT_EQ(hashmap_size(event_map), size_before + 1);
    void *ptr = nullptr;
    hashmap_get_ptr(event_map, evt, &ptr);
    EXPECT_EQ(ptr, nullptr);
}

// set_event_record_status copies the head-stream pointer when stm is in capture.
TEST_F(CoreLimiterTest, set_event_record_status_propagates_capture)
{
    void *head = reinterpret_cast<void *>(0x3002);
    void *evt = reinterpret_cast<void *>(0x3003);
    g_cache_streams.num_streams = 0;
    add_stream(head);
    hashmap_put(stream_map, head, head, true);
    set_event_create_status(evt);
    set_event_record_status(evt, head);
    MapValue v;
    hashmap_get(event_map, evt, &v);
    EXPECT_EQ(v.ptr, head);
    EXPECT_TRUE(v.capture_status);
}

// set_event_destroy_status removes the event from event_map.
TEST_F(CoreLimiterTest, set_event_destroy_status)
{
    void *evt = reinterpret_cast<void *>(0x3004);
    set_event_create_status(evt);
    EXPECT_EQ(hashmap_contains(event_map, evt), 1);
    set_event_destroy_status(evt);
    EXPECT_EQ(hashmap_contains(event_map, evt), 0);
}

// set_event_wait_status propagates capture to waiting stm when event is in capture.
TEST_F(CoreLimiterTest, set_event_wait_status_propagates)
{
    void *head = reinterpret_cast<void *>(0x3005);
    void *evt = reinterpret_cast<void *>(0x3006);
    void *waiter = reinterpret_cast<void *>(0x3007);
    g_cache_streams.num_streams = 0;
    add_stream(waiter);
    set_event_create_status(evt);
    // Mark the event as belonging to the head capture chain.
    hashmap_put(event_map, evt, head, true);
    set_event_wait_status(evt, waiter);
    MapValue v;
    hashmap_get(stream_map, waiter, &v);
    EXPECT_EQ(v.ptr, head);
    EXPECT_TRUE(v.capture_status);
}

// set_event_wait_status does nothing when event has no head-stream.
TEST_F(CoreLimiterTest, set_event_wait_status_no_op)
{
    void *evt = reinterpret_cast<void *>(0x3008);
    void *waiter = reinterpret_cast<void *>(0x3009);
    g_cache_streams.num_streams = 0;
    add_stream(waiter);
    set_event_create_status(evt);
    set_event_wait_status(evt, waiter);
    // waiter should not have been marked as captured.
    MapValue v;
    int rc = hashmap_get(stream_map, waiter, &v);
    if (rc == 0) {
        EXPECT_FALSE(v.capture_status);
    }
}

// is_vnpu_alive returns false for out-of-range vnpu_id.
TEST_F(CoreLimiterTest, is_vnpu_alive_boundary)
{
    EXPECT_FALSE(is_vnpu_alive(-1));
    EXPECT_FALSE(is_vnpu_alive(MAX_VNPU));
    EXPECT_FALSE(is_vnpu_alive(MAX_VNPU + 1));
}

// is_vnpu_alive returns true for g_vnpu_id after enpu_global_init (alive-flush thread).
TEST_F(CoreLimiterTest, is_vnpu_alive_self)
{
    // Retry to handle race with the alive-flush thread (period: ~1ms).
    bool alive = false;
    for (int i = 0; i < 100 && !alive; ++i) {
        alive = is_vnpu_alive(g_vnpu_id);
        if (!alive) {
            usleep(100);
        }
    }
    EXPECT_TRUE(alive);
}

// core_limiter is a no-op when !is_core_limit (best-effort policy).
TEST_F(CoreLimiterTest, core_limiter_best_effort_no_op)
{
    MOCKER(is_core_limit).stubs().will(returnValue(false));
    // Should return immediately without blocking.
    core_limiter(nullptr, nullptr, nullptr);
    SUCCEED();
}

// check_timeout returns true when timestamp is recent (within threshold).
TEST_F(CoreLimiterTest, check_timeout_within_threshold)
{
    atomic_uint_fast64_t ts;
    atomic_store(&ts, ns_now());
    EXPECT_TRUE(check_timeout(&ts, NS_PER_S));
}

// check_timeout returns false when timestamp is older than threshold.
TEST_F(CoreLimiterTest, check_timeout_beyond_threshold)
{
    atomic_uint_fast64_t ts;
    atomic_store(&ts, ns_now() - 2 * NS_PER_S);
    EXPECT_FALSE(check_timeout(&ts, NS_PER_S));
}

// add_stream with multiple distinct pointers inserts all into stream_map.
TEST_F(CoreLimiterTest, add_stream_distinct)
{
    void *s1 = reinterpret_cast<void *>(0x5001);
    void *s2 = reinterpret_cast<void *>(0x5002);
    void *s3 = reinterpret_cast<void *>(0x5003);
    g_cache_streams.num_streams = 0;
    add_stream(s1);
    add_stream(s2);
    add_stream(s3);
    EXPECT_EQ(hashmap_contains(stream_map, s1), 1);
    EXPECT_EQ(hashmap_contains(stream_map, s2), 1);
    EXPECT_EQ(hashmap_contains(stream_map, s3), 1);
}

// remove_stream decrements g_cache_streams.num_streams.
TEST_F(CoreLimiterTest, remove_stream_decrements_cache)
{
    void *stm = reinterpret_cast<void *>(0x5004);
    g_cache_streams.num_streams = 0;
    restore_streams(stm);
    int before = g_cache_streams.num_streams;
    remove_stream(NULL, stm);
    EXPECT_LT(g_cache_streams.num_streams, before);
}

// set_event_record_status on a non-captured stream leaves event.ptr == NULL.
TEST_F(CoreLimiterTest, set_event_record_status_no_capture)
{
    void *stm = reinterpret_cast<void *>(0x5005);
    void *evt = reinterpret_cast<void *>(0x5006);
    g_cache_streams.num_streams = 0;
    add_stream(stm);
    set_event_create_status(evt);
    set_event_record_status(evt, stm);
    void *ptr = nullptr;
    bool cap = true;
    hashmap_get_ptr(event_map, evt, &ptr);
    hashmap_get_capture_status(event_map, evt, &cap);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_FALSE(cap);
}

// set_stream_capture(false) clears capture status on the head stream itself.
TEST_F(CoreLimiterTest, set_stream_capture_false_clears_self)
{
    void *stm = reinterpret_cast<void *>(0x5007);
    g_cache_streams.num_streams = 0;
    restore_streams(stm);
    bool capture = true;
    set_stream_capture(&capture, stm);
    capture = false;
    set_stream_capture(&capture, stm);
    MapValue v;
    hashmap_get(stream_map, stm, &v);
    EXPECT_FALSE(v.capture_status);
}

// slide_window_check returns true for the alive vNPU after init.
TEST_F(CoreLimiterTest, slide_window_check_self_alive)
{
    // slide_window_len is 0 by default (zero-filled shmem); set it so the
    // slide window loop can iterate past the current vNPU.
    g_vnpu_sched_context->slide_window_len.store(1, std::memory_order_relaxed);

    // g_vnpu_id is kept alive by the alive-flush thread (period: ~1ms).
    // Retry to handle the race.
    bool alive = false;
    for (int i = 0; i < 100 && !alive; ++i) {
        alive = slide_window_check(g_vnpu_id);
        if (!alive) {
            usleep(100);
        }
    }
    EXPECT_TRUE(alive);
}

// check_and_borrow_timeslice for the alive vNPU does not crash.
TEST_F(CoreLimiterTest, check_and_borrow_timeslice_self)
{
    check_and_borrow_timeslice(g_vnpu_id);
    SUCCEED();
}

// check_and_borrow_timeslice for a dead vNPU is a safe no-op.
TEST_F(CoreLimiterTest, check_and_borrow_timeslice_dead)
{
    int dead = (g_vnpu_id + 1) % MAX_VNPU;
    check_and_borrow_timeslice(dead);
    SUCCEED();
}

// Prefill hooks publish and clear the state observed by the elastic scheduler.
TEST_F(CoreLimiterTest, prefill_hooks_toggle_scheduler_state)
{
    atomic_store(&g_vnpu_sched_context->prefill_state[g_vnpu_id].in_prefill, false);

    EXPECT_EQ(rtBeginPrefill(), ACL_RT_SUCCESS);
    EXPECT_TRUE(is_vnpu_in_prefill(g_vnpu_id));
    EXPECT_EQ(rtEndPrefill(), ACL_RT_SUCCESS);
    EXPECT_FALSE(is_vnpu_in_prefill(g_vnpu_id));
}

// A duplicate begin must not create an unmatched scope or clear the active one.
TEST_F(CoreLimiterTest, duplicate_prefill_begin_is_rejected)
{
    atomic_store(&g_vnpu_sched_context->prefill_state[g_vnpu_id].in_prefill, false);

    ASSERT_EQ(rtBeginPrefill(), ACL_RT_SUCCESS);
    EXPECT_EQ(rtBeginPrefill(), ACL_ERROR_FAILURE);
    EXPECT_TRUE(is_vnpu_in_prefill(g_vnpu_id));
    EXPECT_EQ(rtEndPrefill(), ACL_RT_SUCCESS);
    EXPECT_FALSE(is_vnpu_in_prefill(g_vnpu_id));
}

// Elastic borrowing is disabled while the current owner publishes prefill.
TEST_F(CoreLimiterTest, borrow_is_blocked_by_owner_prefill)
{
    int owner = (g_vnpu_id + 1) % MAX_VNPU;
    atomic_store(&g_vnpu_sched_context->prefill_state[owner].in_prefill, true);
    EXPECT_FALSE(vnpu_borrow_allowed(owner));

    atomic_store(&g_vnpu_sched_context->prefill_state[owner].in_prefill, false);
    EXPECT_TRUE(vnpu_borrow_allowed(owner));
    EXPECT_FALSE(vnpu_borrow_allowed(g_vnpu_id));
}
