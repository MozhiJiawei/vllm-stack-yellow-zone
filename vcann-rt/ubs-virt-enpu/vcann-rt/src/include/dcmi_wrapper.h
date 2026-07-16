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

#ifndef __DCMI_WRAPPER_H__
#define __DCMI_WRAPPER_H__

#include <dcmi_interface_api.h>
#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_PIDS 64

typedef enum
{
    SOC_VERSION_ASCEND_950,
    SOC_VERSION_NOT_ASCEND_950
} soc_version;

// 定义回调函数类型
typedef int (*dcmi_init_func)(void);

typedef int (*dcmi_get_device_utilization_rate_func)(int logic_id, int card_id, int device_id, int input_type,
                                                     unsigned int *utilization_rate);

typedef int (*dcmi_get_device_resource_info_func)(int logic_id, int card_id, int device_id,
                                                  struct dcmi_proc_mem_info *proc_info, int *proc_num);

typedef struct {
    dcmi_init_func init_callback;
    dcmi_get_device_utilization_rate_func get_device_utilization_rate_callback;
    dcmi_get_device_resource_info_func get_device_resource_info_callback;
} dcmi_operations;

extern int enpu_dcmi_get_card_info(uint32_t phy_id, int *card_id, int *device_id, int *logic_id, uint8_t soc_version);
extern int enpu_dcmi_get_device_resource_info(int logic_id, int card_id, int device_id, size_t *used);
extern int enpu_dcmi_get_device_utilization_rate(int logic_id, int card_id, int device_id,
                                                 unsigned int *utilization_rate);
extern int register_callback(uint8_t soc_version);

#if defined(__cplusplus)
}
#endif

#endif