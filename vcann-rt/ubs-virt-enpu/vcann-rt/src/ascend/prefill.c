/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
* ubs-virt-ovs is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/


#include <acl/acl.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "log.h"
#include "npu_manager.h"
#include "runtime_hook.h"
#include "core_limiter.h"

RUNTIME_HOOK_DEFINE(rtBeginPrefill, void)
{
    LOG_DEBUG("Prefill Begin Hook Intercepted");

    if (g_vnpu_sched_context == NULL || g_vnpu_id >= MAX_VNPU) {
        LOG_ERROR("vCANN scheduler is not initialized before rtBeginPrefill");
        return ACL_ERROR_FAILURE;
    }

    int rc = lock_vnpu_schedule_mutex(g_vnpu_id);
    if (rc != 0) {
        LOG_ERROR("Failed to lock vNPU %d prefill mutex, error code=%d", g_vnpu_id, rc);
        return ACL_ERROR_FAILURE;
    }

    prefill_state_t *prefill_state = &g_vnpu_sched_context->prefill_state[g_vnpu_id];
    bool expected = false;
    if (!atomic_compare_exchange_strong(&prefill_state->in_prefill, &expected, true)) {
        LOG_WARN("vNPU %d already in prefill state, skipping", g_vnpu_id);
        unlock_vnpu_schedule_mutex(g_vnpu_id);
        return ACL_ERROR_FAILURE;
    }

    unlock_vnpu_schedule_mutex(g_vnpu_id);
    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtEndPrefill, void)
{
    LOG_DEBUG("Prefill End Hook Intercepted");

    if (g_vnpu_sched_context == NULL || g_vnpu_id >= MAX_VNPU) {
        LOG_ERROR("vCANN scheduler is not initialized before rtEndPrefill");
        return ACL_ERROR_FAILURE;
    }

    int rc = lock_vnpu_schedule_mutex(g_vnpu_id);
    if (rc != 0) {
        LOG_ERROR("Failed to lock vNPU %d prefill mutex, error code=%d", g_vnpu_id, rc);
        return ACL_ERROR_FAILURE;
    }

    prefill_state_t *prefill_state = &g_vnpu_sched_context->prefill_state[g_vnpu_id];
    if (!atomic_load(&prefill_state->in_prefill)) {
        LOG_WARN("vNPU %d not in prefill state, skipping", g_vnpu_id);
        unlock_vnpu_schedule_mutex(g_vnpu_id);
        return ACL_ERROR_FAILURE;
    }

    // Keep in_prefill published while draining the asynchronous work submitted
    // by the model forward. Borrowers coordinate on the same shared mutex, so
    // no non-owner submission window can open before the drain completes.
    synchronize_and_clear_streams();

    bool expected = true;
    if (!atomic_compare_exchange_strong(&prefill_state->in_prefill, &expected, false)) {
        LOG_ERROR("vNPU %d prefill state changed unexpectedly while ending prefill", g_vnpu_id);
        unlock_vnpu_schedule_mutex(g_vnpu_id);
        return ACL_ERROR_FAILURE;
    }

    unlock_vnpu_schedule_mutex(g_vnpu_id);
    return ACL_RT_SUCCESS;
}
