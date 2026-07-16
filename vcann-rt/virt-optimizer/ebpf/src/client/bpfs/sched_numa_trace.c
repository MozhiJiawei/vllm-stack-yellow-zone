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
    u64 move_count;
    u64 swap_count;
};

// Counter for move_count
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} move_counter SEC(".maps");

// Counter for swap_count
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 1);
} swap_counter SEC(".maps");

SEC("tracepoint/sched/sched_move_numa")
int handle_sched_move_numa()
{
    u64 *count = bpf_map_lookup_elem(&move_counter, &first_item);
    if (count) {
        __sync_fetch_and_add(count, 1);
    } else {
        bpf_map_update_elem(&move_counter, &first_item, &addr_one, BPF_ANY);
    }

    return 0;
}

SEC("tracepoint/sched/sched_swap_numa")
int handle_sched_swap_numa()
{
    u64 now = bpf_ktime_get_ns();
    u64 *count = bpf_map_lookup_elem(&swap_counter, &first_item);
    if (count) {
        __sync_fetch_and_add(count, 1);
    } else {
        bpf_map_update_elem(&swap_counter, &first_item, &addr_one, BPF_ANY);
    }

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
        u64 *move_count = bpf_map_lookup_elem(&move_counter, &first_item);
        if (move_count) {
            event->move_count = *move_count;
        }
        u64 *swap_count = bpf_map_lookup_elem(&swap_counter, &first_item);
        if (swap_count) {
            event->swap_count = *swap_count;
        }
        bpf_ringbuf_submit(event, 0);
        bpf_map_update_elem(&move_counter, &first_item, &addr_zero, BPF_ANY);
        bpf_map_update_elem(&swap_counter, &first_item, &addr_zero, BPF_ANY);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";