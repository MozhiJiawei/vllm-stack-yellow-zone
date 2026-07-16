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
#include "core_limiter.h"
#include "log.h"
#include "runtime_hook.h"

RUNTIME_HOOK_DEFINE(rtFftsPlusTaskLaunch, rtFftsPlusTaskInfo_t *fftsPlusTaskInfo, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtFftsPlusTaskLaunch, fftsPlusTaskInfo, stm);
}

RUNTIME_HOOK_DEFINE(rtFftsPlusTaskLaunchWithFlag, rtFftsPlusTaskInfo_t *fftsPlusTaskInfo, rtStream_t stm, uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtFftsPlusTaskLaunchWithFlag, fftsPlusTaskInfo, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtFftsTaskLaunch, rtFftsTaskInfo_t *fftsTaskInfo, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtFftsTaskLaunch, fftsTaskInfo, stm);
}

RUNTIME_HOOK_DEFINE(rtFftsTaskLaunchWithFlag, rtFftsTaskInfo_t *fftsTaskInfo, rtStream_t stm, uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtFftsTaskLaunchWithFlag, fftsTaskInfo, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtStarsTaskLaunch, const void *taskSqe, uint32_t sqeLen, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtStarsTaskLaunch, taskSqe, sqeLen, stm);
}

RUNTIME_HOOK_DEFINE(rtStarsTaskLaunchWithFlag, const void *taskSqe, uint32_t sqeLen, rtStream_t stm, uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtStarsTaskLaunchWithFlag, taskSqe, sqeLen, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtCmoTaskLaunch, rtCmoTaskInfo_t *taskInfo, rtStream_t stm, uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCmoTaskLaunch, taskInfo, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtCmoAddrTaskLaunch, void *cmoAddrInfo, uint64_t destMax, rtCmoOpCode_t cmoOpCode, rtStream_t stm,
                    uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCmoAddrTaskLaunch, cmoAddrInfo, destMax, cmoOpCode, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtBarrierTaskLaunch, rtBarrierTaskInfo_t *taskInfo, rtStream_t stm, uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtBarrierTaskLaunch, taskInfo, stm, flag);
}

RUNTIME_HOOK_DEFINE(rtMultipleTaskInfoLaunch, const void *taskInfo, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMultipleTaskInfoLaunch, taskInfo, stm);
}

RUNTIME_HOOK_DEFINE(rtMultipleTaskInfoLaunchWithFlag, const void *taskInfo, rtStream_t stm, const uint32_t flag)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtMultipleTaskInfoLaunchWithFlag, taskInfo, stm, flag);
}
