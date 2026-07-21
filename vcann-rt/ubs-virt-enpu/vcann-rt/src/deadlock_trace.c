/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#define VCANN_DEADLOCK_TRACE_IMPLEMENTATION 1
#include "deadlock_trace.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(uintptr_t) == 8, "deadlock trace ABI requires 64-bit pointers");
_Static_assert(sizeof(vcann_host_sync_probe_t) == 40, "host sync probe ABI changed");
_Static_assert(sizeof(vcann_kernel_registration_t) == 296, "kernel registry entry ABI changed");

#define VCANN_FUNCTION_MODE_ACL_HANDLE UINT32_MAX

vcann_trace_buffer_t g_vcann_trace = {
    .magic = VCANN_TRACE_MAGIC,
    .abi_version = VCANN_TRACE_ABI_VERSION,
    .capacity = VCANN_TRACE_CAPACITY,
};
vcann_sync_probe_t g_vcann_sync_probe = {0};
vcann_host_sync_probe_t g_vcann_host_sync_probe = {0};
vcann_kernel_registry_t g_vcann_kernel_registry = {
    .magic = VCANN_KERNEL_REGISTRY_MAGIC,
    .abi_version = VCANN_KERNEL_REGISTRY_ABI_VERSION,
    .capacity = VCANN_KERNEL_REGISTRY_CAPACITY,
};

static _Thread_local uint32_t g_trace_tid;
static volatile uint32_t g_trace_initialized;
static volatile uint32_t g_kernel_registry_lock;

static uint64_t trace_now_ns(void)
{
    struct timespec timestamp;
    (void)clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000000000ULL + (uint64_t)timestamp.tv_nsec;
}

static uint32_t trace_tid(void)
{
    if (g_trace_tid == 0) {
        g_trace_tid = (uint32_t)syscall(SYS_gettid);
    }
    return g_trace_tid;
}

static int trace_requested(const char *value)
{
    return value != NULL && (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
                             strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0);
}

static void copy_kernel_name(char destination[VCANN_KERNEL_NAME_CAPACITY], const char *source)
{
    uint32_t index = 0;
    if (source != NULL) {
        while (index + 1 < VCANN_KERNEL_NAME_CAPACITY && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
    while (++index < VCANN_KERNEL_NAME_CAPACITY) {
        destination[index] = '\0';
    }
}

void vcann_trace_init(void)
{
    uint32_t enabled = (uint32_t)trace_requested(getenv("ENPU_DEADLOCK_TRACE"));
    uint32_t process_id = (uint32_t)getpid();
    if (__atomic_load_n(&g_trace_initialized, __ATOMIC_ACQUIRE) != 0 &&
        g_vcann_trace.process_id == process_id &&
        __atomic_load_n(&g_vcann_trace.enabled, __ATOMIC_ACQUIRE) == enabled) {
        return;
    }
    __atomic_store_n(&g_vcann_trace.enabled, 0, __ATOMIC_RELEASE);
    g_vcann_trace.magic = VCANN_TRACE_MAGIC;
    g_vcann_trace.abi_version = VCANN_TRACE_ABI_VERSION;
    g_vcann_trace.capacity = VCANN_TRACE_CAPACITY;
    g_vcann_trace.process_id = process_id;
    g_trace_tid = 0;
    __atomic_store_n(&g_vcann_trace.next_sequence, 0, __ATOMIC_RELAXED);
    if (enabled != 0) {
        memset(g_vcann_trace.slot_locks, 0, sizeof(g_vcann_trace.slot_locks));
        memset(g_vcann_trace.records, 0, sizeof(g_vcann_trace.records));
        memset(&g_vcann_sync_probe, 0, sizeof(g_vcann_sync_probe));
        memset(&g_vcann_host_sync_probe, 0, sizeof(g_vcann_host_sync_probe));
        memset(g_vcann_kernel_registry.entries, 0, sizeof(g_vcann_kernel_registry.entries));
    }
    g_vcann_kernel_registry.magic = VCANN_KERNEL_REGISTRY_MAGIC;
    g_vcann_kernel_registry.abi_version = VCANN_KERNEL_REGISTRY_ABI_VERSION;
    g_vcann_kernel_registry.capacity = VCANN_KERNEL_REGISTRY_CAPACITY;
    __atomic_store_n(&g_vcann_kernel_registry.next_sequence, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_vcann_kernel_registry.dropped, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_kernel_registry_lock, 0, __ATOMIC_RELAXED);
    g_vcann_kernel_registry.reserved = 0;
    __atomic_store_n(&g_vcann_trace.enabled, enabled, __ATOMIC_RELEASE);
    __atomic_store_n(&g_trace_initialized, 1, __ATOMIC_RELEASE);
}

__attribute__((constructor)) static void vcann_trace_constructor(void)
{
    vcann_trace_init();
}

void vcann_trace_record_enabled(vcann_trace_kind_t kind, rtStream_t stream, const void *object,
                                const void *auxiliary, uint64_t value, uint32_t blocks,
                                uint32_t args_size)
{
    uint64_t sequence = __atomic_fetch_add(&g_vcann_trace.next_sequence, 1, __ATOMIC_RELAXED) + 1;
    uint32_t slot = (uint32_t)((sequence - 1) % VCANN_TRACE_CAPACITY);
    while (__atomic_exchange_n(&g_vcann_trace.slot_locks[slot], 1, __ATOMIC_ACQUIRE) != 0) {
        /* A collision is only possible after a full ring wrap. */
    }
    vcann_trace_record_t *record = &g_vcann_trace.records[slot];
    if (__atomic_load_n(&record->committed_sequence, __ATOMIC_ACQUIRE) > sequence) {
        /* A delayed writer must not overwrite a newer generation of this slot. */
        __atomic_store_n(&g_vcann_trace.slot_locks[slot], 0, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&record->committed_sequence, 0, __ATOMIC_RELAXED);
    record->timestamp_ns = trace_now_ns();
    record->stream = (uintptr_t)stream;
    record->object = (uintptr_t)object;
    record->auxiliary = (uintptr_t)auxiliary;
    record->value = value;
    record->kind = (uint32_t)kind;
    record->blocks = blocks;
    record->args_size = args_size;
    record->tid = trace_tid();
    __atomic_store_n(&record->committed_sequence, sequence, __ATOMIC_RELEASE);
    __atomic_store_n(&g_vcann_trace.slot_locks[slot], 0, __ATOMIC_RELEASE);
}

void vcann_trace_sync_begin_enabled(rtStream_t stream, int32_t owner, uint32_t schedule_turn,
                                    uint32_t vnpu_id)
{
    vcann_trace_record_enabled(VCANN_TRACE_SCHED_SYNC_BEGIN, stream, NULL, NULL, schedule_turn, 0, 0);
    g_vcann_sync_probe.vnpu_id = vnpu_id;
    g_vcann_sync_probe.owner = owner;
    g_vcann_sync_probe.schedule_turn = schedule_turn;
    g_vcann_sync_probe.begin_ns = trace_now_ns();
    g_vcann_sync_probe.begin_sequence = __atomic_load_n(&g_vcann_trace.next_sequence, __ATOMIC_ACQUIRE);
    g_vcann_sync_probe.stream = (uintptr_t)stream;
    __atomic_store_n(&g_vcann_sync_probe.active, 1, __ATOMIC_RELEASE);
}

void vcann_trace_sync_end_enabled(rtStream_t stream)
{
    vcann_trace_record_enabled(VCANN_TRACE_SCHED_SYNC_END, stream, NULL, NULL, 0, 0, 0);
    __atomic_store_n(&g_vcann_sync_probe.active, 0, __ATOMIC_RELEASE);
}

void vcann_trace_host_sync_begin_enabled(vcann_trace_kind_t kind, rtStream_t stream,
                                         int32_t timeout)
{
    vcann_trace_record_enabled(kind, stream, NULL, NULL, (uint32_t)timeout, 0, 0);
    g_vcann_host_sync_probe.kind = (uint32_t)kind;
    g_vcann_host_sync_probe.tid = trace_tid();
    g_vcann_host_sync_probe.timeout = timeout;
    g_vcann_host_sync_probe.begin_ns = trace_now_ns();
    g_vcann_host_sync_probe.begin_sequence =
        __atomic_load_n(&g_vcann_trace.next_sequence, __ATOMIC_ACQUIRE);
    g_vcann_host_sync_probe.stream = (uintptr_t)stream;
    __atomic_store_n(&g_vcann_host_sync_probe.active, 1, __ATOMIC_RELEASE);
}

void vcann_trace_host_sync_end_enabled(vcann_trace_kind_t kind, rtStream_t stream,
                                       int32_t result)
{
    vcann_trace_record_enabled(kind, stream, NULL, NULL, (uint32_t)result, 0, 0);
    __atomic_store_n(&g_vcann_host_sync_probe.active, 0, __ATOMIC_RELEASE);
}

static void kernel_registry_lock(void)
{
    while (__atomic_exchange_n(&g_kernel_registry_lock, 1, __ATOMIC_ACQUIRE) != 0) {
        /* Registration is infrequent and the protected section is fixed-size. */
    }
}

static void kernel_registry_unlock(void)
{
    __atomic_store_n(&g_kernel_registry_lock, 0, __ATOMIC_RELEASE);
}

static vcann_kernel_registration_t *kernel_registry_slot_locked(uint64_t *sequence)
{
    uint64_t count = __atomic_load_n(&g_vcann_kernel_registry.next_sequence, __ATOMIC_RELAXED);
    if (count > VCANN_KERNEL_REGISTRY_CAPACITY) {
        count = VCANN_KERNEL_REGISTRY_CAPACITY;
    }
    for (uint64_t index = 0; index < count; ++index) {
        vcann_kernel_registration_t *entry = &g_vcann_kernel_registry.entries[index];
        if (entry->committed_sequence == index + 1 && entry->handle == 0) {
            *sequence = index + 1;
            return entry;
        }
    }
    if (count >= VCANN_KERNEL_REGISTRY_CAPACITY) {
        __atomic_fetch_add(&g_vcann_kernel_registry.dropped, 1, __ATOMIC_RELAXED);
        return NULL;
    }
    *sequence = count + 1;
    __atomic_store_n(&g_vcann_kernel_registry.next_sequence, *sequence, __ATOMIC_RELAXED);
    return &g_vcann_kernel_registry.entries[count];
}

static void kernel_registry_write_locked(vcann_kernel_registration_t *entry, uint64_t sequence,
                                         void *handle, const void *stub, const char *stub_name,
                                         const void *device_function, uint32_t function_mode)
{
    __atomic_store_n(&entry->committed_sequence, 0, __ATOMIC_RELAXED);
    entry->handle = (uintptr_t)handle;
    entry->stub = (uintptr_t)stub;
    entry->device_function = (uintptr_t)device_function;
    entry->function_mode = function_mode;
    entry->tid = trace_tid();
    copy_kernel_name(entry->stub_name, stub_name);
    /* device_function is opaque in some Runtime versions; never dereference it. */
    copy_kernel_name(entry->device_name, stub_name);
    __atomic_store_n(&entry->committed_sequence, sequence, __ATOMIC_RELEASE);
}

void vcann_trace_kernel_register_enabled(void *handle, const void *stub, const char *stub_name,
                                         const void *device_function, uint32_t function_mode)
{
    uint64_t sequence = 0;
    kernel_registry_lock();
    vcann_kernel_registration_t *entry = kernel_registry_slot_locked(&sequence);
    if (entry != NULL) {
        kernel_registry_write_locked(entry, sequence, handle, stub, stub_name, device_function,
                                     function_mode);
    }
    kernel_registry_unlock();
    if (entry == NULL) {
        return;
    }
    vcann_trace_record_enabled(VCANN_TRACE_KERNEL_REGISTER, NULL, handle, stub, function_mode, 0, 0);
}

void vcann_trace_kernel_map_handle_enabled(void *handle, const void *binary_handle,
                                           const char *kernel_name)
{
    if (handle == NULL || kernel_name == NULL) {
        return;
    }
    char truncated_name[VCANN_KERNEL_NAME_CAPACITY];
    copy_kernel_name(truncated_name, kernel_name);
    uintptr_t address = (uintptr_t)handle;
    bool duplicate = false;
    uint64_t sequence = 0;
    vcann_kernel_registration_t *slot = NULL;
    kernel_registry_lock();
    uint64_t count = __atomic_load_n(&g_vcann_kernel_registry.next_sequence, __ATOMIC_ACQUIRE);
    if (count > VCANN_KERNEL_REGISTRY_CAPACITY) {
        count = VCANN_KERNEL_REGISTRY_CAPACITY;
    }
    for (uint64_t index = 0; index < count; ++index) {
        vcann_kernel_registration_t *entry = &g_vcann_kernel_registry.entries[index];
        if (__atomic_load_n(&entry->committed_sequence, __ATOMIC_ACQUIRE) == index + 1 &&
            __atomic_load_n(&entry->handle, __ATOMIC_ACQUIRE) == address &&
            entry->function_mode == VCANN_FUNCTION_MODE_ACL_HANDLE) {
            if (__atomic_load_n(&entry->stub, __ATOMIC_ACQUIRE) ==
                    (uintptr_t)binary_handle &&
                memcmp(entry->stub_name, truncated_name, VCANN_KERNEL_NAME_CAPACITY) == 0) {
                duplicate = true;
                break;
            }
            /* The opaque handle was reused; invalidate the stale name first. */
            __atomic_store_n(&entry->handle, 0, __ATOMIC_RELEASE);
            break;
        }
    }
    if (!duplicate) {
        slot = kernel_registry_slot_locked(&sequence);
        if (slot != NULL) {
            kernel_registry_write_locked(slot, sequence, handle, binary_handle, truncated_name,
                                         NULL, VCANN_FUNCTION_MODE_ACL_HANDLE);
        }
    }
    kernel_registry_unlock();
    if (slot != NULL) {
        vcann_trace_record_enabled(VCANN_TRACE_KERNEL_REGISTER, NULL, handle, binary_handle,
                                   VCANN_FUNCTION_MODE_ACL_HANDLE, 0, 0);
    }
}

void vcann_trace_kernel_unregister_enabled(void *handle)
{
    uintptr_t address = (uintptr_t)handle;
    kernel_registry_lock();
    uint64_t count = __atomic_load_n(&g_vcann_kernel_registry.next_sequence, __ATOMIC_ACQUIRE);
    if (count > VCANN_KERNEL_REGISTRY_CAPACITY) {
        count = VCANN_KERNEL_REGISTRY_CAPACITY;
    }
    for (uint64_t index = 0; index < count; ++index) {
        vcann_kernel_registration_t *entry = &g_vcann_kernel_registry.entries[index];
        if (__atomic_load_n(&entry->handle, __ATOMIC_ACQUIRE) == address ||
            (entry->function_mode == VCANN_FUNCTION_MODE_ACL_HANDLE &&
             __atomic_load_n(&entry->stub, __ATOMIC_ACQUIRE) == address)) {
            __atomic_store_n(&entry->handle, 0, __ATOMIC_RELEASE);
        }
    }
    kernel_registry_unlock();
    vcann_trace_record_enabled(VCANN_TRACE_KERNEL_UNREGISTER, NULL, handle, NULL, 0, 0, 0);
}
