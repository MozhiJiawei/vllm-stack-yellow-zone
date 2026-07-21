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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include "core_limiter.h"
#include "deadlock_trace.h"
#include "log.h"
#include "npu_manager.h"
#include "runtime_hook.h"

#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
RUNTIME_HOOK_DEFINE(rtStreamSynchronize, rtStream_t stm)
{
    runtime_hook_resolve(HOOK_rtStreamSynchronize);
    vcann_trace_host_sync_begin(VCANN_TRACE_STREAM_SYNC_BEGIN, stm, -1);
    rtError_t ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamSynchronize, stm);
    vcann_trace_host_sync_end(VCANN_TRACE_STREAM_SYNC_END, stm, ret);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDeviceSynchronize, void)
{
    runtime_hook_resolve(HOOK_rtDeviceSynchronize);
    vcann_trace_host_sync_begin(VCANN_TRACE_DEVICE_SYNC_BEGIN, NULL, -1);
    rtError_t ret = RUNTIME_HOOK_CALL(rt_library_entry, rtDeviceSynchronize);
    vcann_trace_host_sync_end(VCANN_TRACE_DEVICE_SYNC_END, NULL, ret);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDeviceSynchronizeWithTimeout, int32_t timeout)
{
    runtime_hook_resolve(HOOK_rtDeviceSynchronizeWithTimeout);
    vcann_trace_host_sync_begin(VCANN_TRACE_DEVICE_SYNC_BEGIN, NULL, timeout);
    rtError_t ret = RUNTIME_HOOK_CALL(rt_library_entry, rtDeviceSynchronizeWithTimeout, timeout);
    vcann_trace_host_sync_end(VCANN_TRACE_DEVICE_SYNC_END, NULL, ret);
    return ret;
}
#endif

pthread_once_t pre_rt_init_flag = PTHREAD_ONCE_INIT;

void load_rt_libraries(void)
{
    int i;
    for (i = 0; i < RUNTIME_ENTRY_END; i++) {
        rt_library_entry[i].func_ptr = dlsym(RTLD_NEXT, rt_library_entry[i].name);
        if (rt_library_entry[i].func_ptr == NULL) {
            LOG_WARN("Failed to find function %s, because the runtime version you are using is different "
                     "from our preset version.",
                     rt_library_entry[i].name);
        }
    }
    return;
}

// Helper function to validate device ID and find the corresponding device context
static int validate_and_set_device_context(int devId, int *device_index)
{
    int device_count = get_device_count();
    
    if (devId < 0) {
        LOG_ERROR("Invalid device ID: %d", devId);
        return ACL_ERROR_INVALID_PARAM;
    }
    
    // Check if we already have a valid active device context
    int current_active = get_active_device_id();
    if (current_active >= 0 && current_active < device_count) {
        // Use the current active device if the requested devId is not specifically for a different device
        if (devId == 0 || devId == get_device_id()) {
            *device_index = current_active;
            return ACL_RT_SUCCESS;
        }
    }
    
    // Find device by requested physical NPU ID if provided
    if (devId != 0) {
        // Get all device configurations to find the one matching devId
        for (int i = 0; i < device_count; i++) {
            npu_info info;
            if (get_device_info_by_index(i, &info) == ENPU_SUCCESS) {
                // Check if this device's logic_id matches the requested devId
                if (info.logic_id == devId) {
                    set_active_device_id(i);
                    *device_index = i;
                    LOG_DEBUG("Found and set active device index %d (logic ID %d) for requested devId %d", 
                             i, devId, devId);
                    return ACL_RT_SUCCESS;
                }
            }
        }
    }
    
    // Default to first device if devId=0 or not found
    *device_index = 0;
    set_active_device_id(0);
    LOG_INFO("DevId %d not found, using default device index %d", devId, *device_index);
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDevice, int32_t devId)
{
    enpu_global_init();
   // CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
   //     "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    
    int device_index = INVALID_VALUE;
    int rc = validate_and_set_device_context(devId, &device_index);
    CHECK_COND_RETURN_((rc != ACL_RT_SUCCESS), rc, "Failed to set device context for devId %d.", devId);
    
    // Get the actual device ID for the runtime API call
    int runtime_device_id = (device_index >= 0 && device_index < MAX_NPU_DEVICES) ? 
                             g_npu_info_array[device_index].logic_id : get_device_id();
    
    LOG_DEBUG("Hook init rtSetDevice devId:%d -> device_index:%d, runtime_device_id:%" PRIi32 ".", 
              devId, device_index, runtime_device_id);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.",
        VNPU_SCHEULE_PERIOD / NS_PER_MS, get_core_limit_quota());

    pthread_once(&pre_rt_init_flag, load_rt_libraries);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDevice, runtime_device_id);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDevice fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceEx, int32_t devId)
{
    enpu_global_init();
    //CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
    //    "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    
    int device_index = INVALID_VALUE;
    int rc = validate_and_set_device_context(devId, &device_index);
    CHECK_COND_RETURN_((rc != ACL_RT_SUCCESS), rc, "Failed to set device context for devId %d.", devId);
    
    // Get the actual device ID for the runtime API call
    int runtime_device_id = (device_index >= 0 && device_index < MAX_NPU_DEVICES) ? 
                            g_npu_info_array[device_index].logic_id : get_device_id();
    
    LOG_DEBUG("Hook init rtSetDeviceEx devId:%d -> device_index:%d, runtime_device_id:%" PRIi32 ".", 
              devId, device_index, runtime_device_id);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.",
        VNPU_SCHEULE_PERIOD / NS_PER_MS, get_core_limit_quota());
    
    pthread_once(&pre_rt_init_flag, load_rt_libraries);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceEx, runtime_device_id);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceEx fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceWithFlags, int32_t devId, uint64_t flags)
{
    enpu_global_init();
    //CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
    //    "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    
    int device_index = INVALID_VALUE;
    int rc = validate_and_set_device_context(devId, &device_index);
    CHECK_COND_RETURN_((rc != ACL_RT_SUCCESS), rc, "Failed to set device context for devId %d.", devId);
    
    // Get the actual device ID for the runtime API call
    int runtime_device_id = (device_index >= 0 && device_index < MAX_NPU_DEVICES) ? 
                            g_npu_info_array[device_index].logic_id : get_device_id();
    
    LOG_DEBUG("Hook init rtSetDeviceWithFlags devId:%d -> device_index:%d, runtime_device_id:%" PRIi32 ".", 
              devId, device_index, runtime_device_id);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.",
        VNPU_SCHEULE_PERIOD / NS_PER_MS, get_core_limit_quota());
    
    pthread_once(&pre_rt_init_flag, load_rt_libraries);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceWithFlags, runtime_device_id, flags);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceWithFlags fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtSetDeviceWithoutTsd, int32_t devId)
{
    enpu_global_init();
    // CHECK_COND_RETURN_(!check_init_success(), ACL_ERROR_UNINITIALIZE,
    //    "Failed to initialize vcann-rt, please check the config file in %s.", NPU_CONFIG_PATH);
    
    int device_index = INVALID_VALUE;
    int rc = validate_and_set_device_context(devId, &device_index);
    CHECK_COND_RETURN_((rc != ACL_RT_SUCCESS), rc, "Failed to set device context for devId %d.", devId);
    
    // Get the actual device ID for the runtime API call
    int runtime_device_id = (device_index >= 0 && device_index < MAX_NPU_DEVICES) ? 
                            g_npu_info_array[device_index].logic_id : get_device_id();
    
    LOG_DEBUG("Hook init rtSetDeviceWithoutTsd devId:%d -> device_index:%d, runtime_device_id:%" PRIi32 ".", 
              devId, device_index, runtime_device_id);
    LOG_DEBUG("The total time slice length is: %zd, and %zd %% of it is available.",
        VNPU_SCHEULE_PERIOD / NS_PER_MS, get_core_limit_quota());
    
    pthread_once(&pre_rt_init_flag, load_rt_libraries);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtSetDeviceWithoutTsd, runtime_device_id);
    CHECK_COND_RETURN_((ret != ACL_RT_SUCCESS), ret, "Call rtSetDeviceWithoutTsd fails, ret:%d.", ret);
    enpu_global_init_post();
    return ACL_RT_SUCCESS;
}
