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

#include "npu_manager.h"
#include <runtime/rt.h>
#include "acl/acl.h"
#include "common.h"
#include "config.h"
#include "core_limiter.h"
#include "deadlock_trace.h"
#include "dcmi_wrapper.h"
#include "include/common.h"
#include "mem_limiter.h"
#include "runtime_hook.h"
#include "utils.h"

// Thread-local device context for each thread
static int active_device_index = INVALID_VALUE;

#define SOC_VERSION_SIZE 64

// Array to store all NPU device information
struct npu_info g_npu_info_array[MAX_NPU_DEVICES];

static int g_npu_device_count = 0;
static pthread_once_t once_init = PTHREAD_ONCE_INIT;
static pthread_once_t post_init_flag = PTHREAD_ONCE_INIT;

// Per-device scheduler context pointers
//static vnpu_time_slice_sched_t* g_device_sched_contexts[MAX_NPU_DEVICES];

// Thread-local context management
int get_active_device_id(void)
{
    return active_device_index;
}

void set_active_device_id(int device_index)
{
    if (device_index >= 0 && device_index < MAX_NPU_DEVICES) {
        active_device_index = device_index;
    } else {
        active_device_index = INVALID_VALUE;
    }
}

// Getter functions that operate on the active device
bool is_core_limit(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].is_core_limit : false;
}

size_t get_mem_limit_quota(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].mem_limit_quota : 0;
}

void set_mem_limit_quota(size_t mem)
{
    int idx = get_active_device_id();
    if (idx >= 0 && idx < MAX_NPU_DEVICES) {
        g_npu_info_array[idx].mem_limit_quota = mem;
    }
}

uint8_t get_core_limit_quota(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].core_limit_quota : 0;
}

int get_device_id(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].device_id : INVALID_VALUE;
}

int get_logic_id(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].logic_id : INVALID_VALUE;
}

uint8_t get_vnpu_id(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].vnpu_id : INVALID_VALUE;
}

uint8_t get_soc_version(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].soc_version : 0;
}

char *get_vnpu_shm_id(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].shm_id : NULL;
}

uint64_t get_core_quota_timeslice(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].core_quota_timeslice : 0;
}

void set_core_quota_timeslice(uint64_t time)
{
    int idx = get_active_device_id();
    if (idx >= 0 && idx < MAX_NPU_DEVICES) {
        g_npu_info_array[idx].core_quota_timeslice = time;
    }
}

int64_t get_core_cur_timeslice(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].core_cur_timeslice : 0;
}

void set_core_cur_timeslice(int64_t time)
{
    int idx = get_active_device_id();
    if (idx >= 0 && idx < MAX_NPU_DEVICES) {
        g_npu_info_array[idx].core_cur_timeslice = time;
    }
}

int get_card_id(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].card_id : INVALID_VALUE;
}

schedule_policy_t get_sched_policy(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].sched_policy : SCHED_POLICY_FIXED_SHARE;
}

bool check_init_success(void)
{
    int idx = get_active_device_id();
    return (idx >= 0 && idx < MAX_NPU_DEVICES) ? g_npu_info_array[idx].initialization : false;
}

int get_device_info_by_index(int device_index, npu_info *info)
{
    if (!info || device_index < 0 || device_index >= MAX_NPU_DEVICES) {
        LOG_ERROR("Invalid parameters: index=%d, info=%p", device_index, info);
        return ENPU_FAIL;
    }
    
    if (device_index >= g_npu_device_count) {
        LOG_ERROR("Device index %d exceeds initialized device count %d", 
                 device_index, g_npu_device_count);
        return ENPU_FAIL;
    }
    
    *info = g_npu_info_array[device_index];
    return ENPU_SUCCESS;
}

// Memory usage for active device
int get_mem_used(size_t *used)
{
    if (!used) {
        LOG_ERROR("used parameter is NULL");
        return ENPU_FAIL;
    }
    
    int idx = get_active_device_id();
    if (idx < 0 || idx >= MAX_NPU_DEVICES) {
        LOG_ERROR("Invalid active device index: %d", idx);
        return ENPU_FAIL;
    }
    
    struct npu_info *npu = &g_npu_info_array[idx];
    int rc = enpu_dcmi_get_device_resource_info(npu->logic_id, npu->card_id, npu->device_id, used);
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to get device resource info for device %d", idx);
    }
    
    return rc;
}

// Initialize device context
int enpu_config_info(int phy_npu_id)
{
    struct ConfigSection *section_config = NULL;
    
    // First determine if we're using a single or multi-device config
    struct MultiConfig *mc = &multi_config;
    
    // Find the configuration for this physical NPU ID
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        if (mc->sections[i].phy_npu_id == phy_npu_id) {
            section_config = &mc->sections[i];
            break;
        }
    }
    
    // If not found in multi-config, check single config
    if (!section_config && multi_config.device_count == 0 && config.phy_npu_id == phy_npu_id) {
        // Use single backward-compatible config - convert to ConfigSection format
        static struct ConfigSection single_section;
        single_section.section_id = 0;
        single_section.phy_npu_id = config.phy_npu_id;
        single_section.vnpu_id = config.vnpu_id;
        single_section.aicore_quota = config.aicore_quota;
        single_section.memory_quota = config.memory_quota;
        single_section.scheduling_policy = config.scheduling_policy;
        (void)strncpy_s(single_section.shm_id, sizeof(single_section.shm_id), config.shm_id, SHM_ID_LEN - 1);
        section_config = &single_section;
    }
    
    if (!section_config) {
        LOG_ERROR("No configuration found for physical NPU ID: %d", phy_npu_id);
        return ENPU_FAIL;
    }
    
    struct ConfigSection *cfg = section_config;
    
    // Find existing slot for this device
    int device_index = -1;
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        if (g_npu_info_array[i].pnpu_id == phy_npu_id) {
            device_index = i;
            break;
        }
    }
    
    if (device_index != -1) {
        LOG_ERROR("Device already inited %d", device_index);
        return ENPU_FAIL;
    }
    
    device_index = g_npu_device_count;

    struct npu_info *npu = &g_npu_info_array[device_index];
    
    // Initialize device configuration
    npu->pnpu_id = cfg->phy_npu_id;
    npu->vnpu_id = cfg->vnpu_id;
    
    // Validate and set quotas
    size_t max_memory_quota = SIZE_MAX / MB_TO_B;
    CHECK_RETURN_RANGE_INT((size_t)cfg->memory_quota, 1, max_memory_quota);
    
    if (cfg->scheduling_policy == SCHED_POLICY_FIXED_SHARE ||
        cfg->scheduling_policy == SCHED_POLICY_ELASTIC) {
        CHECK_RETURN_RANGE_INT(cfg->aicore_quota, 1, MAX_CORE_QUOTA);
        npu->core_limit_quota = (uint8_t)cfg->aicore_quota;
        npu->mem_limit_quota = (size_t)cfg->memory_quota * MB_TO_B;
        npu->is_core_limit = true;
    } else if (cfg->scheduling_policy == SCHED_POLICY_BEST_EFFORT) {
        npu->mem_limit_quota = (size_t)cfg->memory_quota * MB_TO_B;
        npu->is_core_limit = false;
    } else {
        LOG_ERROR("Invalid scheduling policy: %d, should be 0-%d", 
                 cfg->scheduling_policy, SCHED_POLICY_BEST_EFFORT);
        return ENPU_FAIL;
    }
    
    npu->sched_policy = cfg->scheduling_policy;
    
    // Copy shared memory ID
    int ret = strncpy_s(npu->shm_id, sizeof(npu->shm_id), cfg->shm_id, SHM_ID_LEN - 1);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "Failed to copy shm_id");
    
    // Update device count if this is a new device
    if (g_npu_device_count <= device_index) {
        g_npu_device_count = device_index + 1;
    }
    
    LOG_INFO("Initialized NPU device %d: physical NPU ID %d, virtual NPU ID %d, core quota %d, memory quota %lld MB",
             device_index, npu->pnpu_id, npu->vnpu_id, npu->core_limit_quota, 
             (long long)npu->mem_limit_quota / MB_TO_B);
    
    return device_index;  // Return the device index for reference
}

int enpu_device_init(int phy_npu_id)
{
    int device_index = enpu_config_info(phy_npu_id);
    if (device_index < 0) {
        LOG_ERROR("Failed to initialize device for physical NPU ID: %d", phy_npu_id);
        return ENPU_FAIL;
    }
    
    struct npu_info *npu = &g_npu_info_array[device_index];

    LOG_INFO("Got device_index %d for phy npu id %d", device_index, phy_npu_id);

    // Get device information from DCMI
    int card_id = -1, device_id = -1, logic_id = -1;
    int rc = enpu_dcmi_get_card_info(npu->pnpu_id, &card_id, &device_id, &logic_id, npu->soc_version);
    CHECK_RETURN_ERROR_CODE(rc, "Failed to get card info for device %d (physical NPU ID %d)", 
                            device_index, phy_npu_id);
    
    // Store device information
    npu->card_id = card_id;
    npu->device_id = device_id;
    npu->logic_id = logic_id;
    
    LOG_INFO("Device %d initialized: physical NPU ID %d, card ID %d, device ID %d, logic ID %d",
             device_index, npu->pnpu_id, card_id, device_id, logic_id);
    
    return ENPU_SUCCESS;
}

// Multi-device initialization
int enpu_init_devices(void)
{
    struct MultiConfig *mc = &multi_config;
    int rc = ENPU_SUCCESS;
    
    LOG_INFO("Initializing %d NPU devices", mc->device_count);
    
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        if (mc->sections[i].phy_npu_id != INVALID_VALUE) {
            int result = enpu_device_init(mc->sections[i].phy_npu_id);
            if (result != ENPU_SUCCESS) {
                LOG_ERROR("Failed to initialize device %d for physical NPU ID %d", 
                         i, mc->sections[i].phy_npu_id);
                rc = ENPU_FAIL;
                continue;
            }
        }
    }
    
    if (rc == ENPU_SUCCESS) {
        LOG_INFO("Successfully initialized %d NPU devices", g_npu_device_count);
    }
    
    return rc;
}

int enpu_soc_init(void)
{
    uint8_t soc_version;
    const char *socName = aclrtGetSocName();
    CHECK_COND_RETURN_ERROR_CODE(socName == NULL, "Call aclrtGetSocName fails.");
    LOG_INFO("Get socName: %s.", socName);

    if (strstr(socName, "Ascend950") != NULL) {
        soc_version = SOC_VERSION_ASCEND_950;
    } else {
        soc_version = SOC_VERSION_NOT_ASCEND_950;
    }
    int ret = register_callback(soc_version);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to register callback for soc version %s", 
                            soc_version);
    // Initialize SOC for all configured NPU devices
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        g_npu_info_array[i].soc_version = soc_version;
        g_npu_info_array[i].pnpu_id = INVALID_VALUE;
    }

    return ENPU_SUCCESS;
}

int enpu_load_config(void)
{
    int rc = load_config(NPU_CONFIG_PATH);
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to load configuration from %s", NPU_CONFIG_PATH);
        return ENPU_FAIL;
    }
    
    // Check if we loaded a single-device or multi-device config
    if (multi_config.device_count > 0) {
        LOG_INFO("Loaded multi-device configuration with %d devices", multi_config.device_count);
        return ENPU_SUCCESS;
    } else if (config.phy_npu_id != INVALID_VALUE) {
        LOG_INFO("Loaded single-device configuration for physical NPU ID %d", config.phy_npu_id);
        return enpu_config_info(config.phy_npu_id);
    }
    
    LOG_ERROR("No valid device configuration found");
    return ENPU_FAIL;
}

int enpu_load_multi_config(void)
{
    int rc = load_multi_config(NPU_CONFIG_PATH);
    if (rc != ENPU_SUCCESS) {
        LOG_ERROR("Failed to load multi-device configuration from %s", NPU_CONFIG_PATH);
        return ENPU_FAIL;
    }
    
    LOG_INFO("Multi-NPU configuration loaded with %d devices", multi_config.device_count);
    return enpu_init_devices();
}

static void __enpu_global_init(void)
{
    vcann_trace_init();
    int rc = log_init();
    CHECK_COND_RETURN_LOG(rc != ENPU_SUCCESS, "Failed to init log module.");

    rc = enpu_load_config();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to load npu config.");

    rc = enpu_soc_init();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize enpu soc.");

    if (multi_config.device_count > 0) {
        rc = enpu_init_devices();
        CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize enpu devices.");
    } else {
        rc = enpu_device_init(config.phy_npu_id);
        CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize enpu device.");
    }

    rc = memory_limiter_init();
    CHECK_COND_RETURN(rc != ENPU_SUCCESS, "Failed to initialize memory limiter");

    LOG_INFO("Global NPU initialization completed");
}

void enpu_global_init(void)
{
    pthread_once(&once_init, __enpu_global_init);
}

static void __enpu_global_init_post(void)
{
    size_t freeSize = 0;
    size_t totalSize = 0;
    size_t appliedSize = get_mem_limit_quota();
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtMemGetInfoEx, RT_MEMORYINFO_HBM, &freeSize, &totalSize);
    LOG_DEBUG("Call rtMemGetInfoEx return:%d, free HBM size:%zu, total HBM size:%zu, user applied HBM size:%zu.", ret,
              freeSize, totalSize, appliedSize);
    CHECK_COND_LOG_((ret != RT_ERROR_NONE), "Get avaliable HBM size failed! ret:%d, freeSize:%zu, totalSize:%zu.", ret,
                    freeSize, totalSize);
    if (appliedSize > totalSize) {
        LOG_WARN("User appiled HBM size:%zd is bigger than total HBM size:%zd, now set mem_limit_quota to %zd.",
                 appliedSize, totalSize, totalSize);
        set_mem_limit_quota(totalSize);
    }

    ret = aicore_limiter_initialize();
    CHECK_COND_RETURN_LOG(ret != ENPU_SUCCESS, "Failed to init aicore limiter.");

    // Set global environment variable to indicate successful initialization
    ret = setenv("ENPU_ENABLE", "True", 1);
    CHECK_COND_RETURN(ret != 0, "Failed to set ENPU_ENABLE environment variable");

    // Mark all devices as initialized
    for (int i = 0; i < g_npu_device_count; i++) {
        g_npu_info_array[i].initialization = true;
    }

    LOG_INFO("Successfully initialized all NPU devices.");
}

void enpu_global_init_post(void)
{
    pthread_once(&post_init_flag, __enpu_global_init_post);
}
