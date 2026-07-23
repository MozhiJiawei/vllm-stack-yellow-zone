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

    prefill_state_t *prefill_state = &g_vnpu_sched_context->prefill_state[g_vnpu_id];
    bool expected = false;
    if (!atomic_compare_exchange_strong(&prefill_state->in_prefill, &expected, true)) {
        LOG_WARN("vNPU %d already in prefill state, skipping", g_vnpu_id);
        return ACL_ERROR_FAILURE;
    }

    return ACL_RT_SUCCESS;
}

RUNTIME_HOOK_DEFINE(rtEndPrefill, void)
{
    LOG_DEBUG("Prefill End Hook Intercepted");

    prefill_state_t *prefill_state = &g_vnpu_sched_context->prefill_state[g_vnpu_id];
    bool expected = true;
    if (!atomic_compare_exchange_strong(&prefill_state->in_prefill, &expected, false)) {
        LOG_WARN("vNPU %d not in prefill state, skipping", g_vnpu_id);
        return ACL_ERROR_FAILURE;
    }

    return ACL_RT_SUCCESS;
}
