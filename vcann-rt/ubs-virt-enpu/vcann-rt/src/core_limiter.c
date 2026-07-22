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
#include "core_limiter.h"
#include "deadlock_trace.h"
#include "common.h"
#include "dcmi_wrapper.h"
#include "hash_map.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "utils.h"

vnpu_time_slice_sched_t *g_vnpu_sched_context = NULL;
uint8_t g_vnpu_id = 0;
volatile int g_terminate = 0;
atomic_bool g_sched_locking = false;
static _Thread_local unsigned int g_core_gate_depth = 0;
static _Thread_local bool g_core_gate_pending = false;
atomic_int hasModelExecuteSync = 0;
pthread_mutex_t g_sched_mutex;

cache_streams_t g_cache_streams = {.num_streams = 0, .streams = {NULL}};

HashMap *stream_map = NULL;
HashMap *event_map = NULL;

uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_S + (uint64_t)ts.tv_nsec;
}

/// The input must be less than 1000000000.
void ns_sleep(uint64_t ns)
{
    if (ns == 0) {
        return;
    }
    struct timespec req;
    struct timespec rem;
    req.tv_sec = 0;
    req.tv_nsec = ns;
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break;
        }
    }
}

void restore_streams(rtStream_t stream)
{
    if (hashmap_contains(stream_map, (void *)stream)) {
        return;
    }

    if (g_cache_streams.num_streams >= MAX_STREAMS_PER_PROCESS) {
        LOG_ERROR("Failed to add stream %p to the cache. Maximum capacity (%d) reached.", (void *)stream,
                  MAX_STREAMS_PER_PROCESS);
        return;
    }

    g_cache_streams.streams[g_cache_streams.num_streams++] = stream;
    int ret = hashmap_put(stream_map, (void *)stream, NULL, false);
    CHECK_COND_RETURN(ret == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    LOG_DEBUG("Stream %p is added in stream hash map.", (void *)stream);
    return;
}

void add_stream(rtStream_t stream)
{
    if (hashmap_contains(stream_map, (void *)stream)) {
        return;
    }
    int ret = hashmap_put(stream_map, (void *)stream, NULL, false);
    CHECK_COND_RETURN(ret == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    LOG_DEBUG("Stream %p is added in stream hash map.", (void *)stream);
    return;
}

void core_limiter(rtStream_t stream, core_function func, void *param)
{
    // when schedule policy is 3
    if (!is_core_limit()) {
        return;
    }
    if (g_core_gate_depth > 0) {
        restore_streams(stream);
        if (func != NULL) func(param, stream);
        ++g_core_gate_depth;
        g_core_gate_pending = true;
        return;
    }
    while (!g_terminate) {
        // g_sched_locking is a atomic_int for scheduler thread obtain lock with high priority
        if (atomic_load(&g_sched_locking)) {
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }
        LOG_DEBUG("Core limiter is waiting for the mutex lock. g_vnpu_id %d", g_vnpu_id);
        // waiting for mutex == waiting for launch task
        int rc = pthread_mutex_lock(&g_sched_mutex);
        CHECK_COND_RETURN(rc != 0, "Failed to lock mutex, error code=%d.", rc);
        if (det_sched_requested() && !det_sched_has_lease()) {
            pthread_mutex_unlock(&g_sched_mutex);
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }
        LOG_DEBUG("The mutex lock is successfully obtained.");
        // The delivered stream needs to be recorded because the execution time needs to be collected later.
        restore_streams(stream);
        if (func != NULL) {
            func(param, stream);
        }
        g_core_gate_depth = 1;
        g_core_gate_pending = true;
        return;
    }

    return;
}

bool core_limiter_take_pending(void)
{
    bool pending = g_core_gate_pending;
    g_core_gate_pending = false;
    return pending;
}

void core_limiter_release(void)
{
    if (g_core_gate_depth == 0 || --g_core_gate_depth > 0) {
        return;
    }
    atomic_store(&g_vnpu_sched_context->last_kernel_time_ns[g_vnpu_id], ns_now());
    pthread_mutex_unlock(&g_sched_mutex);
}

bool det_sched_gate(rtStream_t stream, core_function func, void *param)
{
    if (!det_sched_requested()) {
        return false;
    }
    core_limiter(stream, func, param);
    return true;
}

bool check_timeout(atomic_uint_fast64_t *timestamp, uint64_t timeout_period)
{
    uint64_t last = atomic_load(timestamp);
    uint64_t now = ns_now();
    // Reboot will recount ns_now() from 0 but not reset timestamp which stored in shared memory.
    // This check is necessary for the problem described above.
    if (last < now) {
        return (now - last <= timeout_period);
    } else {
        return (last - now <= timeout_period);
    }
}

bool is_vnpu_alive(int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        return false;
    }
    return check_timeout(&g_vnpu_sched_context->last_alive_time_ns[vnpu_id], VNPU_TIMEOUT_PERIOD);
}

bool is_vnpu_in_prefill(int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        return false;
    }
    return atomic_load(&g_vnpu_sched_context->prefill_state[vnpu_id].in_prefill);
}

int lock_vnpu_schedule_mutex(int vnpu_id)
{
    if (g_vnpu_sched_context == NULL || vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        return EINVAL;
    }

    int rc = pthread_mutex_lock(&g_vnpu_sched_context->vnpu_schedule_mutex[vnpu_id]);
    if (rc == EOWNERDEAD) {
        LOG_INFO("The scheduling process for vNPU %d exited; taking over its robust mutex.", vnpu_id);
        rc = pthread_mutex_consistent(&g_vnpu_sched_context->vnpu_schedule_mutex[vnpu_id]);
    }
    return rc;
}

void unlock_vnpu_schedule_mutex(int vnpu_id)
{
    if (g_vnpu_sched_context == NULL || vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        return;
    }
    int rc = pthread_mutex_unlock(&g_vnpu_sched_context->vnpu_schedule_mutex[vnpu_id]);
    CHECK_COND_RETURN(rc != 0, "Failed to unlock vNPU %d schedule mutex, error code=%d.", vnpu_id, rc);
}

bool vnpu_borrow_allowed(int owner)
{
    return owner != g_vnpu_id && !is_vnpu_in_prefill(owner);
}

inline bool vnpu_has_work(int vnpu_id)
{
    return check_timeout(&g_vnpu_sched_context->last_kernel_time_ns[vnpu_id], VNPU_NO_TASK_TIMEOUT_PERIOD);
}

bool vnpu_sched_need_skip(void)
{
    schedule_policy_t sched_policy = get_sched_policy();
    if (sched_policy != SCHED_POLICY_ELASTIC) {
        return false;
    }

    if (vnpu_has_work(g_vnpu_id)) {
        return false;
    }

    return true;
}

void vnpu_idling(void)
{
    int npu_core_limit_quota = 0;
    for (int i = 0; i < MAX_VNPU; ++i) {
        if (is_vnpu_alive(i)) {
            npu_core_limit_quota += atomic_load(&g_vnpu_sched_context->vnpu_core_limit_quota[i]);
        }
        if (npu_core_limit_quota > HUNDRED_PERCENT) {
            return;
        }
    }
    ns_sleep((HUNDRED_PERCENT - npu_core_limit_quota) * VNPU_SCHEULE_PERIOD / HUNDRED_PERCENT);
}

int select_next_owner(int vnpu_id)
{
    int next_vnpu_id = -1;

    for (int i = 1; i <= MAX_VNPU; ++i) {
        if (is_vnpu_alive((vnpu_id + i) % MAX_VNPU)) {
            next_vnpu_id = (vnpu_id + i) % MAX_VNPU;
            break;
        }
    }

    return next_vnpu_id;
}

void set_vnpu_and_idle(int vnpu_id, int next_vnpu_id)
{
    if (next_vnpu_id == -1) {
        return;
    }
    if (get_sched_policy() == SCHED_POLICY_FIXED_SHARE && next_vnpu_id <= vnpu_id) {
        vnpu_idling();
    }
    atomic_store(&g_vnpu_sched_context->owner, next_vnpu_id);
}

int synchronize_and_clear_streams_checked(void)
{
    int remaining_count = 0;
    int first_error = ACL_RT_SUCCESS;
    for (int i = 0; i < g_cache_streams.num_streams; ++i) {
        rtStream_t stm = g_cache_streams.streams[i];
        bool capture = 0;
        int rc = hashmap_get_capture_status(stream_map, (void *)stm, &capture);
        if (rc == -1) {
            LOG_ERROR("Failed to get stream %p capture_status from the hash map.", (void *)stm);
            g_cache_streams.streams[remaining_count++] = stm;
            first_error = ACL_ERROR_FAILURE;
            continue;
        }
        if (capture) {
            LOG_DEBUG("Stream %p is in capture, skip synchronization and clear.", (void *)stm);
            g_cache_streams.streams[remaining_count++] = stm;
            first_error = ACL_ERROR_FAILURE;
            continue;
        }
        // int32_t devID = 0;
        // aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtGetDevice, &devID);
        // LOG_DEBUG("Get current Device %d returned with error code %d", devID, ret);
        LOG_DEBUG("Stream %p is being synchronized.", (void*)stm);
        if (vcann_trace_is_enabled()) {
            int32_t owner = atomic_load(&g_vnpu_sched_context->owner);
            uint32_t turn = atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]);
            vcann_trace_sync_begin(stm, owner, turn, g_vnpu_id);
        }
        int sync_rc = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamSynchronize, stm);
        vcann_trace_sync_end(stm);
        if (sync_rc != ACL_RT_SUCCESS) {
            LOG_ERROR("Stream %p synchronization failed, error code=%d.", (void *)stm, sync_rc);
            g_cache_streams.streams[remaining_count++] = stm;
            if (first_error == ACL_RT_SUCCESS) {
                first_error = sync_rc;
            }
            continue;
        }
        LOG_DEBUG("Stream synchronization end.");
        rc = hashmap_remove(stream_map, (void *)stm);
        if (rc == -1) {
            LOG_ERROR("Failed to remove stream %p from the hash map.", (void *)stm);
            g_cache_streams.streams[remaining_count++] = stm;
            first_error = ACL_ERROR_FAILURE;
        }
    }
    g_cache_streams.num_streams = remaining_count;
    return first_error;
}

void synchronize_and_clear_streams(void)
{
    (void)synchronize_and_clear_streams_checked();
}

void compensate_delta_time(void)
{
    uint64_t begin = ns_now();
    synchronize_and_clear_streams();
    uint64_t elapsed = ns_now() - begin;
    set_core_cur_timeslice(get_core_cur_timeslice() - (int64_t)elapsed);
}

bool add_and_consume_time_slice(uint8_t *turn_id)
{
    uint64_t now = ns_now();
    uint8_t current_quota = atomic_load(&g_vnpu_sched_context->vnpu_core_limit_quota[g_vnpu_id]);
    int64_t quota_timeslice = current_quota * VNPU_SCHEULE_PERIOD / HUNDRED_PERCENT;
    int64_t timeslice = get_core_cur_timeslice() + quota_timeslice;

    set_core_cur_timeslice(timeslice);

    if (timeslice <= 0) {
        int vnpu_id = atomic_load(&g_vnpu_sched_context->owner);
        int next_vnpu_id = select_next_owner(vnpu_id);
        set_vnpu_and_idle(vnpu_id, next_vnpu_id);
        return false;
    }

    pthread_mutex_unlock(&g_sched_mutex);

    uint64_t end = now + (uint64_t)timeslice; // 类型转换无安全风险
    set_core_cur_timeslice(0LL);

    // For Determining whether the current round of scheduling is complete for a container with multiple threads.
    *turn_id = atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]);

    bool in_prefill = is_vnpu_in_prefill(g_vnpu_id);
    while (end > now || in_prefill) {
        now = ns_now();
        if (vnpu_sched_need_skip() && !in_prefill) {
            break;
        }
        ns_sleep(WAITING_SLEEP_PERIOD);
        in_prefill = is_vnpu_in_prefill(g_vnpu_id);
    }

    now = ns_now();
    if (now > end) {
        // We've exceeded the quota: compensate Prefill overrun to improve fairness.
        set_core_cur_timeslice(end - now);
    }

    atomic_store(&g_sched_locking, true);
    pthread_mutex_lock(&g_sched_mutex);
    atomic_store(&g_sched_locking, false);

    return true;
}

void *vnpu_scheduler_flush_thread(void *arg)
{
    (void)arg;
    while (!g_terminate) {
        uint64_t now = ns_now();
        atomic_store(&g_vnpu_sched_context->last_alive_time_ns[g_vnpu_id], now);
        ns_sleep(VNPU_FLUSH_PERIOD);
    }
    return NULL;
}

int calculate_alive_vnpu_num(void)
{
    int count = 0;
    for (size_t i = 0; i < MAX_VNPU; i++) {
        if (is_vnpu_alive(i)) {
            count++;
        }
    }
    return count;
}

void *npu_utilization_monitor_thread(void *arg)
{
    (void)arg;
    unsigned int utilization_rate = 0;
    uint64_t begin = ns_now();
    atomic_store(&g_vnpu_sched_context->last_slide_window_time_ns, begin);
    int ret = enpu_dcmi_get_device_utilization_rate(get_logic_id(), get_card_id(), get_device_id(), &utilization_rate);
    if (ret != ENPU_SUCCESS) {
        LOG_ERROR("DCMI call failed with ret: %d.", ret);
        return NULL;
    }

    uint64_t now = ns_now();
    uint64_t diff_ns = now - begin;
    if (diff_ns > DCMI_TIMEOUT_THRESHOLD) {
        LOG_DEBUG("The DCMI interface is overloaded, reuse the NPU utilization status from the last time.");
        return NULL;
    }

    static int high_load_streak = 0;
    static int low_load_streak = 0;
    int current_window = atomic_load(&g_vnpu_sched_context->slide_window_len);
    int new_window = current_window;

    if (utilization_rate > UTILIZATION_RATE_MAX) {
        low_load_streak = 0;
        high_load_streak++;
        if (high_load_streak >= MAX_STREAK && current_window > 0) {
            new_window = current_window - 1;
            high_load_streak = 0;
            LOG_DEBUG("Utilization high (%u%%), decreasing window to %d.", utilization_rate, new_window);
        }
    } else if (utilization_rate < UTILIZATION_RATE_MIN) {
        high_load_streak = 0;
        low_load_streak++;
        if (low_load_streak >= MAX_STREAK) {
            int max_len = calculate_alive_vnpu_num() - 1;
            max_len = (max_len < 0) ? 0 : max_len;
            if (current_window < max_len) {
                new_window = current_window + 1;
                LOG_DEBUG("Utilization low (%u%%), increasing window to %d (max:%d).", utilization_rate, new_window,
                          max_len);
            }
            low_load_streak = 0;
        }
    } else {
        high_load_streak = 0;
        low_load_streak = 0;
    }

    if (new_window != current_window) {
        atomic_store(&g_vnpu_sched_context->slide_window_len, new_window);
    }
    return NULL;
}

bool slide_window_check(int owner)
{
    int slide_windows_len = atomic_load(&g_vnpu_sched_context->slide_window_len);

    for (int i = 1; i <= MAX_VNPU && slide_windows_len > 0; ++i) {
        int next_vnpu = (owner + i) % MAX_VNPU;
        if (next_vnpu == g_vnpu_id) {
            return true;
        }
        // The slide window only contains alive vnpu.
        if (is_vnpu_alive(next_vnpu)) {
            slide_windows_len -= 1;
        }
    }
    return false;
}

void check_and_borrow_timeslice(int owner)
{
    if (owner == g_vnpu_id) {
        return;
    }

    bool owner_mutex_locked = false;
    if (owner >= 0 && owner < MAX_VNPU) {
        // Hold the owner's process-shared mutex for the complete borrow window.
        // rtBeginPrefill uses the same mutex, so no new prefill can overlap a
        // borrow that has already started, and no new borrow can enter prefill.
        int rc = lock_vnpu_schedule_mutex(owner);
        CHECK_COND_RETURN(rc != 0, "Failed to lock owner vNPU %d schedule mutex, error code=%d.", owner, rc);
        owner_mutex_locked = true;
        if (!vnpu_borrow_allowed(owner)) {
            unlock_vnpu_schedule_mutex(owner);
            return;
        }
    }

    pthread_mutex_unlock(&g_sched_mutex);
    ns_sleep(BORROW_TIMESLICE_LENGTH); // borrow BORROW_TIMESLICE_LENGTH ns every time
    atomic_store(&g_sched_locking, true);
    int rc = pthread_mutex_lock(&g_sched_mutex);
    atomic_store(&g_sched_locking, false);
    if (owner_mutex_locked) {
        unlock_vnpu_schedule_mutex(owner);
    }
    CHECK_COND_RETURN(rc != 0, "Failed to lock scheduler mutex after borrowing, error code=%d.", rc);
}

// Scheduling main thread
void *vnpu_scheduler_thread(void *arg)
{
    (void)arg;
    uint8_t turn_id = -1;
    // For scheduler thread:
    //     holding mutex: user can not launch task by core_limiter
    //     release mutex: user can launch task by core_limiter
    pthread_mutex_lock(&g_sched_mutex);

    int logic_id = get_logic_id();
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDevice, logic_id);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), NULL, "Call rtSetDevice fails after vnpu scheduler is created, ret: %d, target : %d", ret, logic_id);

    while (!g_terminate) {
        if (det_sched_requested()) {
            det_sched_fail_if_participant_lost();
            if (det_sched_has_lease()) {
                pthread_mutex_unlock(&g_sched_mutex);
                while (!g_terminate && det_sched_has_lease()) {
                    ns_sleep(WAITING_SLEEP_PERIOD);
                }
                atomic_store(&g_sched_locking, true);
                pthread_mutex_lock(&g_sched_mutex);
                atomic_store(&g_sched_locking, false);
            } else {
                ns_sleep(WAITING_SLEEP_PERIOD);
            }
            continue;
        }
        // Distributed thread scheduling.
        // Scheduling is performed only when the owner is the current vnpu or the owner is disabled.
        int owner = atomic_load(&g_vnpu_sched_context->owner);

        // ELASTIC will consider borrow timeslice
        if (get_sched_policy() == SCHED_POLICY_ELASTIC) {
            check_and_borrow_timeslice(owner);
        }

        if (owner != g_vnpu_id) {
            if (!is_vnpu_alive(owner)) {
                int vnpu_id = atomic_load(&g_vnpu_sched_context->owner);
                set_vnpu_and_idle(vnpu_id, select_next_owner(vnpu_id));
            }
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }

        // Consumption time slice. The lock is released to the user process within the specified time.
        bool flag = add_and_consume_time_slice(&turn_id);


        // Only one thread is accepted.
        int rc = lock_vnpu_schedule_mutex(g_vnpu_id);
        if (rc != 0) {
            LOG_WARN("Failed to obtain mutex lock, error code=%d.", rc);
            continue;
        }

        // if (previous scheduler not successed)
        if (atomic_load(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id]) == turn_id) {
            // Only the slice of the main process is considered.
            // Multi-process in the same vNPU is an unrecommended scenario and should be avoided as much as possible.
            if (flag) {
                compensate_delta_time();
                int vnpu_id = atomic_load(&g_vnpu_sched_context->owner);
                set_vnpu_and_idle(vnpu_id, select_next_owner(vnpu_id));
            }
            atomic_store(&g_vnpu_sched_context->vnpu_schedule_turn[g_vnpu_id], turn_id + 1);
        }
        unlock_vnpu_schedule_mutex(g_vnpu_id);
    }
    pthread_mutex_unlock(&g_sched_mutex);
    hashmap_destroy(stream_map);
    hashmap_destroy(event_map);
    return NULL;
}

void share_mem_init(vnpu_time_slice_sched_t *vnpu_sched_shm)
{
    g_vnpu_sched_context = vnpu_sched_shm;

    while (!g_terminate) {
        if (atomic_load(&g_vnpu_sched_context->magic_number) == MAGIC_INITIALIZED) {
            return;
        }

        if (atomic_load(&g_vnpu_sched_context->magic_number) == MAGIC_INITIALIZING) {
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }

        uint_fast32_t expected = MAGIC_UNINITIALIZED;
        if (!atomic_compare_exchange_strong(&g_vnpu_sched_context->magic_number,
                                            &expected, MAGIC_INITIALIZING)) {
            if (expected != MAGIC_INITIALIZING && expected != MAGIC_INITIALIZED) {
                LOG_ERROR("Incompatible shared-memory layout 0x%lx", (unsigned long)expected);
                g_terminate = 1;
                return;
            }
            continue;
        }
        atomic_store(&g_vnpu_sched_context->owner, -1);

        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        for (int i = 0; i < MAX_VNPU; ++i) {
            atomic_store(&g_vnpu_sched_context->last_alive_time_ns[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->last_kernel_time_ns[i], 0ULL);
            atomic_store(&g_vnpu_sched_context->vnpu_core_limit_quota[i], 0);
            atomic_store(&g_vnpu_sched_context->vnpu_schedule_turn[i], 0);
            atomic_store(&g_vnpu_sched_context->prefill_state[i].in_prefill, false);
            pthread_mutex_init(&g_vnpu_sched_context->vnpu_schedule_mutex[i], &attr);
        }

        det_sched_init(&attr);
        pthread_mutexattr_destroy(&attr);
        atomic_store(&g_vnpu_sched_context->magic_number, MAGIC_INITIALIZED);
        return;
    }
}

int vnpu_scheduler_init(vnpu_time_slice_sched_t *vnpu_sched_shm)
{
    g_vnpu_sched_context = vnpu_sched_shm;
    g_vnpu_id = get_vnpu_id();

    uint8_t aicore_limit_percent = get_core_limit_quota();
    atomic_store(&g_vnpu_sched_context->vnpu_core_limit_quota[g_vnpu_id], aicore_limit_percent);
    uint64_t aicore_cur_timesilice = aicore_limit_percent * VNPU_SCHEULE_PERIOD / HUNDRED_PERCENT;
    set_core_cur_timeslice(0);
    set_core_quota_timeslice(aicore_cur_timesilice);

    LOG_INFO("aicore_limit_percent %d aicore_cur_timesilice %d", aicore_limit_percent, aicore_cur_timesilice);

    if (is_core_limit()) {
        pthread_t vnpu_scheduler_tid;
        int rc = pthread_create(&vnpu_scheduler_tid, NULL, vnpu_scheduler_thread, NULL);
        CHECK_COND_RETURN_ERROR_CODE(rc != 0, "Failed to create vnpu scheduler thread.");

        pthread_t vnpu_alive_tid;
        rc = pthread_create(&vnpu_alive_tid, NULL, vnpu_scheduler_flush_thread, NULL);
        CHECK_COND_RETURN_ERROR_CODE(rc != 0, "Failed to create vnpu alive thread.");
        pthread_detach(vnpu_scheduler_tid);
        pthread_detach(vnpu_alive_tid);
    }
    return ENPU_SUCCESS;
}

int aicore_limiter_initialize(void)
{
    int rc = ENPU_FAIL;
    vnpu_time_slice_sched_t *vnpu_sched_shm = NULL;
    vnpu_sched_shm = map_share_mem(get_vnpu_shm_id(), sizeof(*g_vnpu_sched_context));
    if (vnpu_sched_shm == NULL) {
        LOG_ERROR("Failed to mmap share memory.");
        return ENPU_FAIL;
    }

    share_mem_init(vnpu_sched_shm);
    if (g_terminate) {
        return ENPU_FAIL;
    }
    pthread_mutex_init(&g_sched_mutex, NULL);

    rc = vnpu_scheduler_init(vnpu_sched_shm);
    CHECK_RETURN_ERROR_CODE(rc, "Failed to initialize vnpu scheduler.");

    stream_map = hashmap_create(MAX_STREAMS_PER_PROCESS);
    if (!stream_map) {
        LOG_ERROR("Stream hash map init failed.");
        return ENPU_FAIL;
    }

    event_map = hashmap_create(MAX_EVENT_PER_PROCESS);
    if (!event_map) {
        LOG_ERROR("Event hash map init failed.");
        hashmap_destroy(stream_map);
        return ENPU_FAIL;
    }
    return rc;
}

void set_stream_capture(void *param, rtStream_t stream)
{
    bool capture = *(bool *)param;
    if (!capture) {
        for (int i = 0; i < g_cache_streams.num_streams; ++i) {
            rtStream_t stm = g_cache_streams.streams[i];
            void *head_stream = NULL;
            int rc = hashmap_get_ptr(stream_map, (void *)stm, &head_stream);
            CHECK_COND_RETURN(rc == -1, "Failed to get stream %p ptr from the hash map.", (void *)stm);
            if (head_stream == (void *)stream) {
                LOG_DEBUG("Stream %p capture state set to: 0.", (void *)stream);
                rc = hashmap_put(stream_map, (void *)stm, NULL, false);
                CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stm);
            }
        }
    } else {
        int rc = hashmap_put(stream_map, (void *)stream, (void *)stream, capture);
        CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stream);
    }
    LOG_DEBUG("Stream %p capture state set to: %d.", (void *)stream, capture ? 1 : 0);
}

void set_event_wait_status(void *evt, rtStream_t stm)
{
    MapValue event_status;
    int rc = hashmap_get(event_map, evt, &event_status);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map get event %p failed.", evt);

    // not capture stream
    if (event_status.ptr != NULL) {
        // update capture status by event
        rc = hashmap_put(stream_map, (void *)stm, event_status.ptr, true);
        CHECK_COND_RETURN(rc == -1, "Failed to put stream %p to the hash map.", (void *)stm);
        LOG_DEBUG("Stream %p capture state set to: true, because of event.", (void *)stm);
    }
}

void set_event_create_status(void *evt)
{
    int rc = hashmap_put(event_map, evt, NULL, false);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map put event %p failed.", evt);
}

void set_event_record_status(void *evt, rtStream_t stm)
{
    MapValue event_status;
    int rc = hashmap_get(event_map, evt, &event_status);
    CHECK_COND_RETURN(rc == -1, "Error: Event hash map get event %p failed.", evt);
    void *head_stream = NULL;
    rc = hashmap_get_ptr(stream_map, (void *)stm, &head_stream);
    CHECK_COND_RETURN(rc == -1, "Failed to get stream %p ptr from the hash map.", (void *)stm);
    // capture
    if (head_stream != NULL) {
        rc = hashmap_put(event_map, evt, head_stream, true);
        CHECK_COND_RETURN(rc == -1, "Error: Event hash map put event %p failed.", evt);
        LOG_DEBUG("Event %p capture status is updated to true in recording.", evt);
    }
}

void remove_stream(void *unused, rtStream_t stm)
{
    (void)unused;
    LOG_DEBUG("Remove stream %p", stm);
    for (int i = 0; i < g_cache_streams.num_streams; ++i) {
        if (stm == g_cache_streams.streams[i]) {
            for (int j = i + 1; j < g_cache_streams.num_streams; ++j) {
                g_cache_streams.streams[j - 1] = g_cache_streams.streams[j];
            }
            g_cache_streams.num_streams -= 1;
            hashmap_remove(stream_map, (void *)stm);
            LOG_DEBUG("Stream position %d removed.", i);
            break;
        }
    }
}

void set_event_destroy_status(void *evt)
{
    (void)hashmap_remove(event_map, evt);
}
