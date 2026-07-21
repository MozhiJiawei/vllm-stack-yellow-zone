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

vcann_trace_buffer_t g_vcann_trace = {
    .magic = VCANN_TRACE_MAGIC,
    .abi_version = VCANN_TRACE_ABI_VERSION,
    .capacity = VCANN_TRACE_CAPACITY,
};
vcann_sync_probe_t g_vcann_sync_probe = {0};

static _Thread_local uint32_t g_trace_tid;

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

void vcann_trace_init(void)
{
    uint32_t enabled = (uint32_t)trace_requested(getenv("ENPU_DEADLOCK_TRACE"));
    __atomic_store_n(&g_vcann_trace.enabled, 0, __ATOMIC_RELEASE);
    g_vcann_trace.magic = VCANN_TRACE_MAGIC;
    g_vcann_trace.abi_version = VCANN_TRACE_ABI_VERSION;
    g_vcann_trace.capacity = VCANN_TRACE_CAPACITY;
    g_vcann_trace.process_id = (uint32_t)getpid();
    g_trace_tid = 0;
    __atomic_store_n(&g_vcann_trace.next_sequence, 0, __ATOMIC_RELAXED);
    if (enabled != 0) {
        memset(g_vcann_trace.slot_locks, 0, sizeof(g_vcann_trace.slot_locks));
        memset(g_vcann_trace.records, 0, sizeof(g_vcann_trace.records));
        memset(&g_vcann_sync_probe, 0, sizeof(g_vcann_sync_probe));
    }
    __atomic_store_n(&g_vcann_trace.enabled, enabled, __ATOMIC_RELEASE);
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
