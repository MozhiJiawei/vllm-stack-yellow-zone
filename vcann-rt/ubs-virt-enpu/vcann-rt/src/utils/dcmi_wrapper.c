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
#include "utils.h"

#include "dcmi_wrapper.h"

#define NPU_UTILIZATION (13)
#define MAX_DEVICE_LIST_NUM 64

typedef struct {
    int logic_id;
    int card_id;
    int device_id;
    struct dcmi_proc_mem_info *proc_info;
    int *proc_num;
    int result;
} mem_info_args;

static dcmi_operations g_dcmi_ops = {NULL};

int __attribute__((weak)) dcmiv2_init(void);

int __attribute__((weak)) dcmiv2_get_device_list(int *device_list, int *device_count, int list_len);

int __attribute__((weak))
dcmiv2_get_device_utilization_rate(int dev_id, int input_type, unsigned int *utilization_rate);

int __attribute__((weak))
dcmiv2_get_device_proc_mem_info(int dev_id, struct dcmi_proc_mem_info *proc_info, int *proc_num);

int __attribute__((weak)) dcmi_init(void);

int __attribute__((weak))
dcmi_get_device_utilization_rate(int card_id, int device_id, int input_type, unsigned int *utilization_rate);

int __attribute__((weak))
dcmi_get_device_resource_info(int card_id, int device_id, struct dcmi_proc_mem_info *proc_info, int *proc_num);

int __attribute__((weak))
dcmi_get_card_id_device_id_from_phyid(int *card_id, int *device_id, unsigned int device_phy_id);

int __attribute__((weak)) dcmi_get_device_logic_id(int *device_logic_id, int card_id, int device_id);

int _dcmiv2_init_callback()
{
    return dcmiv2_init();
}

int _dcmi_init_callback()
{
    return dcmi_init();
}

int _dcmiv2_get_device_utilization_rate_callback(int logic_id, int card_id, int device_id, int input_type,
                                                 unsigned int *utilization)
{
    (void)card_id;
    (void)device_id;
    return dcmiv2_get_device_utilization_rate(logic_id, input_type, utilization);
}

int _dcmi_get_device_utilization_rate_callback(int logic_id, int card_id, int device_id, int input_type,
                                               unsigned int *utilization)
{
    (void)logic_id;
    return dcmi_get_device_utilization_rate(card_id, device_id, input_type, utilization);
}

int _dcmiv2_get_device_resource_info_callback(int logic_id, int card_id, int device_id,
                                              struct dcmi_proc_mem_info *proc_info, int *proc_num)
{
    (void)card_id;
    (void)device_id;
    return dcmiv2_get_device_proc_mem_info(logic_id, proc_info, proc_num);
}

int _dcmi_get_device_resource_info_callback(int logic_id, int card_id, int device_id,
                                            struct dcmi_proc_mem_info *proc_info, int *proc_num)
{
    (void)logic_id;
    return dcmi_get_device_resource_info(card_id, device_id, proc_info, proc_num);
}

int register_callback(uint8_t soc_version)
{
    if (soc_version == SOC_VERSION_ASCEND_950) {
        g_dcmi_ops.init_callback = _dcmiv2_init_callback;
        g_dcmi_ops.get_device_utilization_rate_callback = _dcmiv2_get_device_utilization_rate_callback;
        g_dcmi_ops.get_device_resource_info_callback = _dcmiv2_get_device_resource_info_callback;
    } else {
        g_dcmi_ops.init_callback = _dcmi_init_callback;
        g_dcmi_ops.get_device_utilization_rate_callback = _dcmi_get_device_utilization_rate_callback;
        g_dcmi_ops.get_device_resource_info_callback = _dcmi_get_device_resource_info_callback;
    }

    if (g_dcmi_ops.init_callback == NULL || g_dcmi_ops.get_device_utilization_rate_callback == NULL ||
        g_dcmi_ops.get_device_resource_info_callback == NULL) {
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int enpu_dcmi_get_card_info(uint32_t phy_id, int *card_id, int *device_id, int *logic_id, uint8_t soc_version)
{
    int ret;
    LOG_INFO("Enpu_dcmi_get_card_info begin");

    if (!card_id || !device_id || !logic_id) {
        LOG_ERROR("Invalid param");
        return ENPU_FAIL;
    }

    ret = g_dcmi_ops.init_callback();
    CHECK_RETURN_ERROR_CODE(ret, "dcmi init failed.");
    LOG_INFO("dcmi_init success");

    if (soc_version == SOC_VERSION_NOT_ASCEND_950) {
        ret = dcmi_get_card_id_device_id_from_phyid(card_id, device_id, phy_id);
        CHECK_RETURN_ERROR_CODE(ret, "get card info failed.");
	ret = dcmi_get_device_logic_id(logic_id, *card_id, *device_id);
	LOG_INFO("get logic id returned %d", ret);
	CHECK_RETURN_ERROR_CODE(ret, "get logic id failed.");
    } else {
        int device_list[MAX_DEVICE_LIST_NUM] = {0};
        int device_cnt = 0;

        ret = dcmiv2_get_device_list(device_list, &device_cnt, sizeof(device_list) / sizeof(device_list[0]));
        CHECK_RETURN_ERROR_CODE(ret, "get device list failed.");
        LOG_INFO("device_cnt = %d, device_list[0] = %d", device_cnt, device_list[0]);
        *logic_id = device_list[0];
        *card_id = -1;
        *device_id = device_list[0]; // The deviceID of the ascend950 chip is exactly the same as the logicID.
    }
    LOG_INFO("enpu_dcmi_get_card_info success");
    return ENPU_SUCCESS;
}

// Get NPU Utilization
int enpu_dcmi_get_device_utilization_rate(int logic_id, int card_id, int device_id, unsigned int *utilization_rate)
{
    // Using NPU total utilization. (Other choices: 2. AICore, 3. AICpu)
    return g_dcmi_ops.get_device_utilization_rate_callback(logic_id, card_id, device_id, NPU_UTILIZATION,
                                                           utilization_rate);
}

static void *enpu_get_resource_info_thread(void *arg)
{
    mem_info_args *args = (mem_info_args *)arg;
    int proc_num_temp = 0;

    args->result = g_dcmi_ops.get_device_resource_info_callback(args->logic_id, args->card_id, args->device_id,
                                                                args->proc_info, &proc_num_temp);

    if (args->proc_num != NULL) {
        *(args->proc_num) = proc_num_temp;
    }
    return NULL;
}

static int enpu_get_mem_used(struct dcmi_proc_mem_info *proc_info, int proc_num, size_t *used)
{
    if (proc_info == NULL || used == NULL) {
        LOG_ERROR("Invalid parameters");
        return ENPU_FAIL;
    }

    if (proc_num > MAX_PIDS) {
        LOG_ERROR("dcmi get device resource too many processes, count is %d", proc_num);
        return DCMI_ERR_CODE_INNER_ERR;
    }

    *used = 0;

    for (int i = 0; i < proc_num; i++) {
        *used += proc_info[i].proc_mem_usage;
        LOG_DEBUG("Contain PID %d, mem usage:%zd", proc_info[i].proc_id, proc_info[i].proc_mem_usage);
    }
    LOG_DEBUG("dcmi get mem used as %zd", *used);
    return ENPU_SUCCESS;
}

int enpu_dcmi_get_device_resource_info(int logic_id, int card_id, int device_id, size_t *used)
{
    int proc_num = 0;
    struct dcmi_proc_mem_info proc_info[MAX_PIDS];

    if (used == NULL) {
        LOG_ERROR("Invalid parameters");
        return ENPU_FAIL;
    }

    memset_s(proc_info, sizeof(proc_info), 0, sizeof(proc_info));
    mem_info_args args = {.logic_id = logic_id,
                          .card_id = card_id,
                          .device_id = device_id,
                          .proc_info = proc_info,
                          .proc_num = &proc_num,
                          .result = ENPU_FAIL};

    pthread_t thread;

    int ret = pthread_create(&thread, NULL, enpu_get_resource_info_thread, &args);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "create thread failed.");

    ret = pthread_join(thread, NULL);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "join thread failed.");

    if (args.result != 0) {
        LOG_ERROR("get info failed card:%d device:%d result:%d", card_id, device_id, args.result);
        return ENPU_FAIL;
    }

    ret = enpu_get_mem_used(proc_info, proc_num, used);
    CHECK_RETURN_ERROR_CODE(ret, "get device mem info failed card-id:%d device_id:%d.", card_id, device_id);

    return ENPU_SUCCESS;
}
