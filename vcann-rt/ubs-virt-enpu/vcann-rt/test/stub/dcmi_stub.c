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

#include <dcmi_interface_api.h>
#include <stdio.h>

int dcmi_init(void)
{
    printf("call stub_dcmi_init\n");
    return 0;
}

int dcmi_get_card_id_device_id_from_logicid(int *card_id, int *device_id, unsigned int device_logic_id)
{
    printf("call stub_dcmi_get_card_id_device_id_from_logicid\n");
    return 0;
}

int dcmi_get_device_resource_info(int card_id, int device_id, struct dcmi_proc_mem_info *proc_info, int *proc_num)
{
    printf("call stub_dcmi_get_device_resource_info\n");
    return 0;
}

int dcmi_get_device_utilization_rate(int card_id, int device_id, int input_type, unsigned int *utilization_rate)
{
    printf("call stub_dcmi_get_device_utilization_rate\n");
    return 0;
}

int dcmi_get_card_id_device_id_from_phyid(int *card_id, int *device_id, unsigned int device_phy_id)
{
    printf("call stub_dcmi_get_card_id_device_id_from_phyid\n");
    return 0;
}

int dcmiv2_init(void)
{
    printf("call stub_dcmiv2_init\n");
    return 0;
}

int dcmiv2_get_device_utilization_rate(int dev_id, int input_type, unsigned int *utilization_rate)
{
    printf("call stub_dcmiv2_get_device_utilization_rate\n");
    return 0;
}

int dcmiv2_get_device_proc_mem_info(int dev_id, struct dcmi_proc_mem_info *proc_info, int *proc_num)
{
    printf("call stub_dcmiv2_get_device_resource_info\n");
    return 0;
}

int dcmiv2_get_device_list(int *device_list, int *device_count, int list_len)
{
    printf("call stub_dcmiv2_get_device_list\n");
    return 0;
}
