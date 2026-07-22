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
#ifndef __NPU_MANAGER_H__
#define __NPU_MANAGER_H__

#if defined(__cplusplus)
#include <atomic>
using atomic_int = std::atomic<int>;
using atomic_uint_fast8_t = std::atomic<uint_fast8_t>;
using atomic_uint_fast32_t = std::atomic<uint_fast32_t>;
using atomic_uint_fast64_t = std::atomic<uint_fast64_t>;
#else
#include <stdatomic.h>
#endif

#include "config.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define NPU_CONFIG_PATH "/etc/enpu/vcann-rt/npu_info.config"
#define MAX_NPU_ID 15
#define MAX_VNPU 100
#define MAX_CORE_QUOTA 100
#define MB_TO_B (1024 * 1024)
#define MAX_DEVICE_LIST_NUM 64
#define MAX_NPU_DEVICES 16  // Maximum number of NPU devices supported
#define DET_SCHED_PARTICIPANTS 2

typedef enum {
    DET_SNAPSHOT_UNKNOWN = 0,
    DET_SNAPSHOT_IDLE = 1,
    DET_SNAPSHOT_READY = 2,
} det_snapshot_t;

typedef enum {
    DET_STATE_DISABLED = 0,
    DET_STATE_PARK = 1,
    DET_STATE_GRANTED_0 = 2,
    DET_STATE_RUNNING_0 = 3,
    DET_STATE_GRANTED_1 = 4,
    DET_STATE_RUNNING_1 = 5,
    DET_STATE_FAILED = 6,
} det_sched_state_t;

typedef enum
{
    SCHED_POLICY_FIXED_SHARE = 1,
    SCHED_POLICY_ELASTIC = 2,
    SCHED_POLICY_BEST_EFFORT = 3,
} schedule_policy_t;

typedef struct {
    atomic_bool in_prefill;
} prefill_state_t;

// Expand shared memory structure to support multiple devices
typedef struct shared_memory {
    pthread_mutex_t vnpu_schedule_mutex[MAX_VNPU];
    atomic_uint_fast64_t last_alive_time_ns[MAX_VNPU];
    atomic_uint_fast64_t last_kernel_time_ns[MAX_VNPU];
    atomic_uint_fast8_t vnpu_schedule_turn[MAX_VNPU];
    atomic_uint_fast8_t vnpu_core_limit_quota[MAX_VNPU];
    atomic_int owner;
    atomic_uint_fast32_t magic_number;
    atomic_int slide_window_len;
    atomic_uint_fast64_t last_slide_window_time_ns;
    prefill_state_t prefill_state[MAX_VNPU];
    pthread_mutex_t det_mutex;
    atomic_int det_participants[DET_SCHED_PARTICIPANTS];
    int det_snapshot[DET_SCHED_PARTICIPANTS];
    atomic_int det_state;
    uint64_t det_weighted_turn;
} vnpu_time_slice_sched_t;

// NPU information structure per device
typedef struct npu_info {
    int32_t pnpu_id;
    int logic_id;
    int card_id;
    int device_id;
    uint8_t vnpu_id;
    bool in_used;
    size_t mem_limit_quota;
    uint8_t core_limit_quota;
    uint64_t core_quota_timeslice;
    int64_t core_cur_timeslice;
    bool is_core_limit;
    schedule_policy_t sched_policy;
    char shm_id[SHM_ID_LEN];
    bool initialization;
    uint8_t soc_version;
} npu_info;

// Global variables
extern void enpu_global_init(void);
extern void enpu_global_init_post(void);

// Multiple NPU device support
int get_active_device_id(void);
void set_active_device_id(int device_id);
int get_device_info_by_index(int device_index, npu_info *info);

// Backward compatibility functions (operate on active device)
extern bool is_core_limit(void);
extern uint8_t get_core_limit_quota(void);
extern size_t get_mem_limit_quota(void);
extern void set_mem_limit_quota(size_t mem);
extern char *get_vnpu_shm_id(void);
extern int get_mem_used(size_t *used);
extern int get_device_id(void);
extern uint8_t get_vnpu_id(void);
extern uint64_t get_core_quota_timeslice(void);
extern void set_core_quota_timeslice(uint64_t time);
extern int64_t get_core_cur_timeslice(void);
extern void set_core_cur_timeslice(int64_t time);
extern int get_card_id(void);
extern schedule_policy_t get_sched_policy(void);
extern bool check_init_success(void);
extern uint8_t get_soc_version(void);
extern int get_logic_id(void);

// Array to store all NPU device information
extern struct npu_info g_npu_info_array[MAX_NPU_DEVICES];

int get_mem_used(size_t *used);

// Multi-device initialization functions
int enpu_load_config(void);
int enpu_load_multi_config(void);
int enpu_init_devices(void);
int enpu_device_init(int phy_npu_id);
int enpu_config_info(int phy_npu_id);
extern int enpu_soc_init(void);
#if defined(__cplusplus)
}
#endif

#endif
