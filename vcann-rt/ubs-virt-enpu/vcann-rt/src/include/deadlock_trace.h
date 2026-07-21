/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#ifndef __DEADLOCK_TRACE_H__
#define __DEADLOCK_TRACE_H__

#include <stdbool.h>
#include <stdint.h>
#include <runtime/rt.h>

#if defined(VCANN_ENABLE_DEADLOCK_DIAGNOSTICS) || defined(VCANN_DEADLOCK_TRACE_IMPLEMENTATION)

#if defined(__cplusplus)
extern "C" {
#endif

#define VCANN_TRACE_MAGIC 0x5643414E4E545243ULL /* "VCANNTRC" */
#define VCANN_TRACE_ABI_VERSION 3U
#define VCANN_TRACE_CAPACITY 4096U
#define VCANN_KERNEL_REGISTRY_MAGIC 0x5643414E4E4B5247ULL /* "VCANNKRG" */
#define VCANN_KERNEL_REGISTRY_ABI_VERSION 1U
#define VCANN_KERNEL_REGISTRY_CAPACITY 512U
#define VCANN_KERNEL_NAME_CAPACITY 128U

/* Keep these values stable: vcann_trace_gdb.py decodes the ABI by number. */
typedef enum vcann_trace_kind {
    VCANN_TRACE_INVALID = 0,
    VCANN_TRACE_RT_KERNEL_LAUNCH = 1,
    VCANN_TRACE_RT_KERNEL_HANDLE = 2,
    VCANN_TRACE_RT_KERNEL_HANDLE_V2 = 3,
    VCANN_TRACE_RT_KERNEL_FLAG = 4,
    VCANN_TRACE_RT_KERNEL_FLAG_V2 = 5,
    VCANN_TRACE_RT_KERNEL_EX = 6,
    VCANN_TRACE_RT_KERNEL_FWK = 7,
    VCANN_TRACE_RT_CPU_KERNEL = 8,
    VCANN_TRACE_RT_AICPU_KERNEL = 9,
    VCANN_TRACE_RT_AICPU_KERNEL_EX = 10,
    VCANN_TRACE_RT_FUNC_HANDLE = 11,
    VCANN_TRACE_RT_FUNC_HANDLE_V2 = 12,
    VCANN_TRACE_RT_FUNC_HANDLE_V3 = 13,
    VCANN_TRACE_RT_VECTOR_HANDLE = 14,
    VCANN_TRACE_RT_VECTOR_KERNEL = 15,
    VCANN_TRACE_RTS_KERNEL_HOST_ARGS = 16,
    VCANN_TRACE_RTS_CPU_KERNEL = 17,
    VCANN_TRACE_RTS_KERNEL_CONFIG = 18,
    VCANN_TRACE_RTS_KERNEL_DEV_ARGS = 19,
    VCANN_TRACE_RTS_RANDOM_TASK = 20,
    VCANN_TRACE_RTS_REDUCE_TASK = 21,
    VCANN_TRACE_RTS_UPDATE_TASK = 22,
    VCANN_TRACE_RT_FFTS_TASK = 23,
    VCANN_TRACE_RT_STARS_TASK = 24,
    VCANN_TRACE_RT_CMO_TASK = 25,
    VCANN_TRACE_RT_BARRIER_TASK = 26,
    VCANN_TRACE_RT_MULTIPLE_TASK = 27,
    VCANN_TRACE_RT_MODEL_EXECUTE = 28,
    VCANN_TRACE_RT_MODEL_EXECUTE_SYNC = 29,
    VCANN_TRACE_EVENT_RECORD = 30,
    VCANN_TRACE_EVENT_WAIT = 31,
    VCANN_TRACE_NOTIFY_RECORD = 32,
    VCANN_TRACE_NOTIFY_WAIT = 33,
    VCANN_TRACE_STREAM_DESTROY = 34,
    VCANN_TRACE_STREAM_CAPTURE_BEGIN = 35,
    VCANN_TRACE_STREAM_CAPTURE_END = 36,
    VCANN_TRACE_SCHED_SYNC_BEGIN = 37,
    VCANN_TRACE_SCHED_SYNC_END = 38,
    VCANN_TRACE_KERNEL_REGISTER = 39,
    VCANN_TRACE_KERNEL_UNREGISTER = 40,
    VCANN_TRACE_DEVICE_SYNC_BEGIN = 41,
    VCANN_TRACE_DEVICE_SYNC_END = 42,
    VCANN_TRACE_STREAM_SYNC_BEGIN = 43,
    VCANN_TRACE_STREAM_SYNC_END = 44,
} vcann_trace_kind_t;

/* committed_sequence is published last; readers ignore partially written slots. */
typedef struct vcann_trace_record {
    volatile uint64_t committed_sequence;
    uint64_t timestamp_ns;
    uintptr_t stream;
    uintptr_t object;
    uintptr_t auxiliary;
    uint64_t value;
    uint32_t kind;
    uint32_t blocks;
    uint32_t args_size;
    uint32_t tid;
} vcann_trace_record_t;

typedef struct vcann_trace_buffer {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t capacity;
    volatile uint32_t enabled;
    uint32_t process_id;
    volatile uint64_t next_sequence;
    uint32_t slot_locks[VCANN_TRACE_CAPACITY];
    vcann_trace_record_t records[VCANN_TRACE_CAPACITY];
} vcann_trace_buffer_t;

typedef struct vcann_sync_probe {
    volatile uint32_t active;
    uint32_t vnpu_id;
    int32_t owner;
    uint32_t schedule_turn;
    uint64_t begin_ns;
    uint64_t begin_sequence;
    uintptr_t stream;
} vcann_sync_probe_t;

typedef struct vcann_host_sync_probe {
    volatile uint32_t active;
    uint32_t kind;
    uint32_t tid;
    int32_t timeout;
    uint64_t begin_ns;
    uint64_t begin_sequence;
    uintptr_t stream;
} vcann_host_sync_probe_t;

/* Names are copied at rtFunctionRegister time so GDB never dereferences stale pointers. */
typedef struct vcann_kernel_registration {
    volatile uint64_t committed_sequence;
    uintptr_t handle;
    uintptr_t stub;
    uintptr_t device_function;
    uint32_t function_mode;
    uint32_t tid;
    char stub_name[VCANN_KERNEL_NAME_CAPACITY];
    char device_name[VCANN_KERNEL_NAME_CAPACITY];
} vcann_kernel_registration_t;

typedef struct vcann_kernel_registry {
    uint64_t magic;
    uint32_t abi_version;
    uint32_t capacity;
    volatile uint64_t next_sequence;
    volatile uint32_t dropped;
    uint32_t reserved;
    vcann_kernel_registration_t entries[VCANN_KERNEL_REGISTRY_CAPACITY];
} vcann_kernel_registry_t;

extern vcann_trace_buffer_t g_vcann_trace;
extern vcann_sync_probe_t g_vcann_sync_probe;
extern vcann_host_sync_probe_t g_vcann_host_sync_probe;
extern vcann_kernel_registry_t g_vcann_kernel_registry;

void vcann_trace_init(void);
void vcann_trace_record_enabled(vcann_trace_kind_t kind, rtStream_t stream, const void *object,
                                const void *auxiliary, uint64_t value, uint32_t blocks,
                                uint32_t args_size);
void vcann_trace_sync_begin_enabled(rtStream_t stream, int32_t owner, uint32_t schedule_turn,
                                    uint32_t vnpu_id);
void vcann_trace_sync_end_enabled(rtStream_t stream);
void vcann_trace_host_sync_begin_enabled(vcann_trace_kind_t kind, rtStream_t stream,
                                         int32_t timeout);
void vcann_trace_host_sync_end_enabled(vcann_trace_kind_t kind, rtStream_t stream,
                                       int32_t result);
void vcann_trace_kernel_register_enabled(void *handle, const void *stub, const char *stub_name,
                                         const void *device_function, uint32_t function_mode);
void vcann_trace_kernel_map_handle_enabled(void *handle, const void *binary_handle,
                                           const char *kernel_name);
void vcann_trace_kernel_unregister_enabled(void *handle);

static inline bool vcann_trace_is_enabled(void)
{
    return __atomic_load_n(&g_vcann_trace.enabled, __ATOMIC_RELAXED) != 0;
}

#define vcann_trace_record(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_record_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_sync_begin(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_sync_begin_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_sync_end(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_sync_end_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_host_sync_begin(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_host_sync_begin_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_host_sync_end(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_host_sync_end_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_kernel_register(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_kernel_register_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_kernel_map_handle(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_kernel_map_handle_enabled(__VA_ARGS__); \
        } \
    } while (0)
#define vcann_trace_kernel_unregister(...) \
    do { \
        if (__builtin_expect(vcann_trace_is_enabled(), 0)) { \
            vcann_trace_kernel_unregister_enabled(__VA_ARGS__); \
        } \
    } while (0)

#if defined(__cplusplus)
}
#endif

#else

#define vcann_trace_init() ((void)0)
#define vcann_trace_is_enabled() (false)
#define vcann_trace_record(...) ((void)0)
#define vcann_trace_sync_begin(...) ((void)0)
#define vcann_trace_sync_end(...) ((void)0)
#define vcann_trace_host_sync_begin(...) ((void)0)
#define vcann_trace_host_sync_end(...) ((void)0)
#define vcann_trace_kernel_register(...) ((void)0)
#define vcann_trace_kernel_map_handle(...) ((void)0)
#define vcann_trace_kernel_unregister(...) ((void)0)

#endif
#endif
