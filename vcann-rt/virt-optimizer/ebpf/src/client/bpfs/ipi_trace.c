/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "bpf_include.h"

// Collected data
struct system_event_data {
    u64 ipi_count;
    u64 transmission_delay_ns;
    u64 processing_delay_ns;
};

// Counter for ipi_count
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} counter SEC(".maps");

// Summation of transmission_delay_ns
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} transmission_delay_ns_map SEC(".maps");

// Summation of processing_delay_ns
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} processing_delay_ns_map SEC(".maps");

// Timestamp of last ipi_raise
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} ipi_raise_map SEC(".maps");

// Timestamp of last ipi_entry
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} ipi_entry_map SEC(".maps");

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_raise")
int handle_ipi_raise()
{
    u64 now = bpf_ktime_get_ns();
    bpf_map_update_elem(&ipi_raise_map, &first_item, &now, BPF_ANY);
    return 0;
}
#endif

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_entry")
#elif defined(__TARGET_ARCH_x86_64)
SEC("tracepoint/irq_vectors/call_function_entry")
#else
undefined
#endif
int handle_ipi_entry()
{
    u64 now = bpf_ktime_get_ns();
    bpf_map_update_elem(&ipi_entry_map, &first_item, &now, BPF_ANY);

#if defined(__aarch64__)
    u64 *ipi_raise_timestamp = bpf_map_lookup_elem(&ipi_raise_map, &first_item);
    if (!ipi_raise_timestamp) {
        return 0;
    }
    u64 *transmission_delay_delta = bpf_map_lookup_elem(&transmission_delay_ns_map, &first_item);

    if (transmission_delay_delta) {
        __sync_fetch_and_add(transmission_delay_delta, now - (*ipi_raise_timestamp));
    } else {
        bpf_map_update_elem(&transmission_delay_ns_map, &first_item, &addr_zero, BPF_ANY);
    }
#endif

    u64 *last_time = bpf_map_lookup_elem(&last_submit_time, &first_item);

    if (!last_time) {
        bpf_map_update_elem(&last_submit_time, &first_item, &now, BPF_ANY);
        return 0;
    }
    if (now - (*last_time) > INTERVAL_NS) {
        bpf_map_update_elem(&last_submit_time, &first_item, &now, BPF_ANY);

        struct system_event_data *event;
        event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
        if (!event) {
            return 0;
        }
        u64 *count = bpf_map_lookup_elem(&counter, &first_item);
        if (count) {
            event->ipi_count = *count;
        }
        u64 *transmission_delay_ns = bpf_map_lookup_elem(&transmission_delay_ns_map, &first_item);
        if (transmission_delay_ns) {
            event->transmission_delay_ns = *transmission_delay_ns;
        }
        u64 *processing_delay_ns = bpf_map_lookup_elem(&processing_delay_ns_map, &first_item);
        if (processing_delay_ns) {
            event->processing_delay_ns = *processing_delay_ns;
        }
        bpf_ringbuf_submit(event, 0);
        bpf_map_update_elem(&counter, &first_item, &addr_zero, BPF_ANY);
        bpf_map_update_elem(&transmission_delay_ns_map, &first_item, &addr_zero, BPF_ANY);
        bpf_map_update_elem(&processing_delay_ns_map, &first_item, &addr_zero, BPF_ANY);
    }
    return 0;
}

#if defined(__TARGET_ARCH_arm64)
SEC("tracepoint/ipi/ipi_exit")
#elif defined(__TARGET_ARCH_x86_64)
SEC("tracepoint/irq_vectors/call_function_exit")
#endif
int handle_ipi_exit()
{
    u64 now = bpf_ktime_get_ns();

    u64 *ipi_entry_timestamp = bpf_map_lookup_elem(&ipi_entry_map, &first_item);
    if (!ipi_entry_timestamp) {
        bpf_map_update_elem(&ipi_entry_map, &first_item, &now, BPF_ANY);
        return 0;
    }

    u64 *count = bpf_map_lookup_elem(&counter, &first_item);
    if (count) {
        __sync_fetch_and_add(count, 1);
    } else {
        bpf_map_update_elem(&counter, &first_item, &addr_one, BPF_ANY);
    }

    u64 *processing_delay_ns = bpf_map_lookup_elem(&processing_delay_ns_map, &first_item);
    if (processing_delay_ns) {
        __sync_fetch_and_add(processing_delay_ns, now - (*ipi_entry_timestamp));
    } else {
        bpf_map_update_elem(&processing_delay_ns_map, &first_item, &addr_zero, BPF_ANY);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";
