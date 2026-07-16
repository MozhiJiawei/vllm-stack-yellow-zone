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
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SHM_ID_LEN 128
#define MAX_NPU_DEVICES 16  // Maximum number of NPU devices supported
#define OPTION_NPU_ID "physical-npu-id"
#define OPTION_VNPU_ID "virtual-npu-id"
#define OPTION_AICORE_QUOTA "aicore-quota"
#define OPTION_MEMORY_QUOTA "memory-quota"
#define OPTION_SHM_ID "shm-id"
#define OPTION_SCHEDULING_POLICY "scheduling-policy"
#define INVALID_VALUE (-1)

// Single device configuration (for backward compatibility)
struct Config {
    int32_t phy_npu_id;
    int32_t vnpu_id;
    int32_t aicore_quota;
    int32_t memory_quota;
    int32_t scheduling_policy;
    char shm_id[SHM_ID_LEN];
};

// Extended structure for multiple NPU devices
struct ConfigSection {
    int32_t section_id;        // Device section index (0 to MAX_NPU_DEVICES-1)
    int32_t phy_npu_id;        // Physical NPU ID
    int32_t vnpu_id;           // Virtual NPU ID within this physical device
    int32_t aicore_quota;     // AI Core resource quota (%)
    int32_t memory_quota;     // Memory quota (MB)
    int32_t scheduling_policy; // Scheduling policy
    char shm_id[SHM_ID_LEN];   // Shared memory ID for this device
};

// Container for all NPU devices
struct MultiConfig {
    int32_t device_count;                  // Number of configured devices
    struct ConfigSection sections[MAX_NPU_DEVICES]; // Array of device configurations
    struct Config single_config;           // For backward compatibility
};

extern struct Config config;               // Backward compatibility
extern struct MultiConfig multi_config;   // New multi-device support

int load_config(const char *file_path);
int load_multi_config(const char *file_path); // Load multi-device config
int get_device_count(void);                // Get number of configured devices
int get_device_config(int index, struct ConfigSection *config); // Get device config by index
void reset_single_config(void);            // Reset single device config
void reset_multi_config(void);             // Reset multi-device config

#if defined(__cplusplus)
}
#endif

#endif