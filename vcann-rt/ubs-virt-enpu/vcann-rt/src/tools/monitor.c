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
#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "npu_manager.h"

/*
static void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    int ret = vfprintf(stderr, fmt, ap);
    CHECK_COND_RETURN(ret < 0, "vfprintf failed.");
    va_end(ap);

    return;
}
*/

static int parse_args(int argc, char *const argv[])
{
    if (argc > 1) {
        printf("Usage: %s [option]\n", argv[0]);
        printf("Options:\n");
        printf("  -h, --help      Show this help message\n");
        printf("  -v, --verbose   Show detailed device information\n");
        printf("  -a, --aggregate Show aggregated usage across all devices\n");
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

static void print_detailed_device_info(int device_index, npu_info *npu)
{
    size_t used;
    int ret = get_mem_used(&used);
    
    printf("   --- NPU Device %d ---\n", device_index);
    printf("     Physical NPU ID          : %u\n", npu->pnpu_id);
    printf("     Device ID Logic          : %d\n", npu->logic_id);
    printf("     Device ID Runtime        : %d\n", npu->device_id);
    printf("     Card ID                  : %d\n", npu->card_id);
    printf("     Virtual NPU ID           : %u\n", npu->vnpu_id);
    printf("     Aicore Limit Quota(%%)   : %hu\n", npu->core_limit_quota);
    printf("     Memory Limit Quota(MB)   : %lld\n", (long long)npu->mem_limit_quota / 1024 / 1024);
    printf("     Memory Usage(MB)         : %lld\n", (long long)used / 1024 / 1024);
    printf("     Scheduling Policy        : %d\n", npu->sched_policy);
    printf("     Status                   : %s\n", npu->initialization ? "Initialized" : "Not Initialized");
    printf("     SoC Version              : %s\n", npu->soc_version ? "Ascend950" : "Other");
    printf("     Shared Memory ID         : %s\n", npu->shm_id);
}

static int check_device_health(int device_index, npu_info *npu)
{
    // This is a placeholder - in reality, you'd implement proper health checks
    size_t used;
    int ret = get_mem_used(&used);
    if (ret != ENPU_SUCCESS) {
        printf("   Device %d: Health check failed (%d)\n", device_index, ret);
        return 0;
    }
    
    // Check memory usage is within limits
    if (used > npu->mem_limit_quota * 0.9) {
        printf("   Device %d: High memory usage (%lld MB of %lld MB, %.1f%%)\n", 
               device_index, (long long)used / 1024 / 1024, 
               (long long)npu->mem_limit_quota / 1024 / 1024,
               (double)used * 100.0 / npu->mem_limit_quota);
        return 0;
    }
    
    return 1;
}

// Monitor for a single active device
/*
static int monitor_single_device(void)
{
    size_t used;
    int ret = get_mem_used(&used);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to get memory usage for active device.");
    
    die("       --- Single Device ---\n"
        "       Aicore Limit Quota(%)     : %d\n"
        "       Memory Limit Quota(MB)    : %lld\n"
        "       Memory Usage(MB)          : %lld\n",
        get_core_limit_quota(), 
        (long long)get_mem_limit_quota() / 1024 / 1024,
        (long long)used / 1024 / 1024);
    
    return ENPU_SUCCESS;
}
*/

// Aggregated monitoring for all devices
static int monitor_devices_aggregated(void)
{
  int device_count = get_device_count();
  size_t total_used = 0;
  size_t total_quota = 0;
  uint8_t total_core_quota = 0;

  if (device_count <= 0) {
    printf("   No NPU devices configured or initialized.\n");
    return ENPU_SUCCESS;
  }

  // Aggregate metrics across all devices
  for (int i = 0; i < device_count; i++) {
    npu_info npu_info;
    int ret = get_device_info_by_index(i, &npu_info);

    if (ret == ENPU_SUCCESS) {
      total_core_quota += npu_info.core_limit_quota;
      total_quota += npu_info.mem_limit_quota;

      // Get memory usage for the first device as a sample
      if (i == 0) {
        ret = get_mem_used(&total_used);
        if (ret != ENPU_SUCCESS) {
          total_used = 0;
        }
      }
    }
   }

   printf("vCANN-RT NPU Monitor - Aggregated (%d devices):\n\n", device_count);
   printf("   --- Aggregate Metrics ---\n");
   printf("     Total Core Limit Quota(%%)   : %d\n", total_core_quota);
   printf("     Total Memory Limit Quota(MB) : %lld\n", (long long)total_quota / 1024 / 1024);
   printf("     Sample Memory Usage(MB)      : %lld\n", (long long)total_used / 1024 / 1024);
   printf("     Aggregate Memory Utilization : %.1f%%\n", (double)total_used * 100.0 / total_quota);
   printf("     Device Count                  : %d\n", device_count);

   return ENPU_SUCCESS;
}

// Monitor all configured devices with detailed information
static int monitor_all_devices_detailed(void)
{
    int device_count = get_device_count();
    
    printf("vCANN-RT NPU Monitor - %d device(s) detected:\n\n", device_count);
    
    if (device_count <= 0) {
        printf("   No NPU devices configured or initialized.\n");
        return ENPU_SUCCESS;
    }
    
    int healthy_devices = 0;
    //size_t total_used = 0;
    //size_t total_quota = 0;
    
    for (int i = 0; i < device_count; i++) {
        npu_info npu_info;
        int ret = get_device_info_by_index(i, &npu_info);
        
        if (ret == ENPU_SUCCESS) {
            print_detailed_device_info(i, &npu_info);
            int is_healthy = check_device_health(i, &npu_info);
            
            if (is_healthy) {
                healthy_devices++;
            }
            
      //      if (!i) {  // First device - get its memory usage as active
//		size_t used;
 //               ret = get_mem_used(&used);
  //              total_used = used;
 //               total_quota = npu_info.mem_limit_quota;
  //          } else {
  //              // For monitoring, we'll use first device's metrics as sample
  //              // In a real implementation, you might want to aggregate
  //          }
            
            printf("\n");
        } else {
            printf("   Device %d: Failed to get device information\n", i);
        }
    }

    monitor_devices_aggregated();
    
    printf("   Summary:\n");
    //printf("     Total Device Count   : %d\n", device_count);
    printf("     Healthy Devices     : %d\n", healthy_devices);
    //printf("     Unhealthy Devices   : %d\n", device_count - healthy_devices);
    //printf("     Total Memory Usage  : %lld MB\n", (long long)total_used / 1024 / 1024);
    //printf("     Total Memory Quota : %lld MB\n", (long long)total_quota / 1024 / 1024);
    //printf("     Memory Utilization  : %.1f%%\n", 
    //       (double)total_used * 100.0 / total_quota);
    
    return ENPU_SUCCESS;
}


// Main monitoring function that determines mode
static int monitor_npu_utilization(void)
{
    // For now, default to detailed multi-device monitoring
    // In the future, this could be controlled by command line options
    return monitor_all_devices_detailed();
}

int main(int argc, char *argv[])
{
    int ret;
    char *env_enpu_level = getenv("ENPU_LOG_LEVEL");
    
    ret = log_init();
    CHECK_RETURN_ERROR_CODE(ret, "Log init failed.");

    ret = parse_args(argc, argv);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to parse args.");

    if (env_enpu_level != NULL) {
        unsetenv("ENPU_LOG_LEVEL");
    }

    ret = enpu_load_config();
    CHECK_RETURN_ERROR_CODE(ret, "Loading NPU configuration failed.");

    ret = enpu_soc_init();
    CHECK_RETURN_ERROR_CODE(ret, "Enpu soc init failed.");

    if (multi_config.device_count > 0) {
        ret = enpu_init_devices();
        CHECK_RETURN_ERROR_CODE(ret, "Failed to initialize enpu devices.");
    } else {
        ret = enpu_device_init(config.phy_npu_id);
        CHECK_RETURN_ERROR_CODE(ret, "Failed to initialize enpu device.");
    }
    
    ret = monitor_npu_utilization();
    CHECK_RETURN_ERROR_CODE(ret, "NPU utilization monitor failed.");

    return ENPU_SUCCESS;
}
