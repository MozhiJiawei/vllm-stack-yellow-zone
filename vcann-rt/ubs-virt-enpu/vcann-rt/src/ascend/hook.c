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
#include "runtime_hook.h"

rt_entry_t rt_library_entry[] = {
    /* Init Part */
    {.name = "rtSetDevice"},
    {.name = "rtSetDeviceEx"},
    {.name = "rtSetDeviceWithFlags"},
    {.name = "rtSetDeviceWithoutTsd"},
    /* Memory Part */
    {.name = "rtMalloc"},
    {.name = "rtMallocCached"},
    {.name = "rtDvppMalloc"},
    {.name = "rtDvppMallocWithFlag"},
    {.name = "rtMemAlloc"},
    {.name = "rtMemAllocManaged"},
    {.name = "rtMallocPhysical"},
    {.name = "rtMemGetInfoEx"},
    /* Kernel Part */
    {.name = "rtKernelLaunch"},
    {.name = "rtKernelLaunchWithHandle"},
    {.name = "rtKernelLaunchWithHandleV2"},
    {.name = "rtKernelLaunchWithFlag"},
    {.name = "rtKernelLaunchWithFlagV2"},
    {.name = "rtKernelLaunchEx"},
    {.name = "rtKernelLaunchFwk"},
    {.name = "rtCpuKernelLaunch"},
    {.name = "rtAicpuKernelLaunch"},
    {.name = "rtCpuKernelLaunchWithFlag"},
    {.name = "rtAicpuKernelLaunchWithFlag"},
    {.name = "rtAicpuKernelLaunchExWithArgs"},
    {.name = "rtLaunchKernelByFuncHandle"},
    {.name = "rtLaunchKernelByFuncHandleV2"},
    {.name = "rtLaunchKernelByFuncHandleV3"},
    {.name = "rtVectorCoreKernelLaunchWithHandle"},
    {.name = "rtVectorCoreKernelLaunch"},
    {.name = "rtFftsPlusTaskLaunch"},
    {.name = "rtFftsPlusTaskLaunchWithFlag"},
    {.name = "rtFftsTaskLaunch"},
    {.name = "rtFftsTaskLaunchWithFlag"},
    {.name = "rtModelExecute"},
    {.name = "rtModelExecuteAsync"},
    {.name = "rtStreamBeginCapture"},
    {.name = "rtStreamEndCapture"},
    {.name = "rtsModelExecute"},
    {.name = "rtModelExecuteSync"},
    {.name = "rtStarsTaskLaunch"},
    {.name = "rtStarsTaskLaunchWithFlag"},
    {.name = "rtCmoTaskLaunch"},
    {.name = "rtCmoAddrTaskLaunch"},
    {.name = "rtBarrierTaskLaunch"},
    {.name = "rtMultipleTaskInfoLaunch"},
    {.name = "rtMultipleTaskInfoLaunchWithFlag"},
    {.name = "rtsModelExecuteAsync"},
    {.name = "rtsLaunchKernelWithHostArgs"},
    {.name = "rtsLaunchCpuKernel"},
    {.name = "rtsLaunchKernelWithConfig"},
    {.name = "rtsLaunchKernelWithDevArgs"},
    {.name = "rtsLaunchRandomNumTask"},
    {.name = "rtsLaunchReduceAsyncTask"},
    {.name = "rtsLaunchUpdateTask"},
#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
    {.name = "aclrtBinaryGetFunction"},
    {.name = "aclrtLaunchKernel"},
    {.name = "aclrtLaunchKernelWithConfig"},
    {.name = "aclrtLaunchKernelV2"},
    {.name = "aclrtLaunchKernelWithHostArgs"},
    {.name = "rtFunctionRegister"},
    {.name = "rtDevBinaryUnRegister"},
#endif
    /* Event Part */
    {.name = "rtEventCreate"},
    {.name = "rtsEventCreate"},
    {.name = "rtsEventCreateEx"},
    {.name = "rtEventCreateWithFlag"},
    {.name = "rtEventCreateExWithFlag"},
    {.name = "rtStreamWaitEvent"},
    {.name = "rtEventRecord"},
    {.name = "rtEventDestroy"},
    {.name = "rtEventReset"},
    {.name = "rtsNotifyCreate"},
    {.name = "rtNotifyRecord"},
    {.name = "rtNotifyDestroy"},
    {.name = "rtsNotifyWaitAndReset"},
    {.name = "rtStreamWaitEventWithTimeout"},
    {.name = "rtEventDestroySync"},
    {.name = "rtNotifyCreate"},
    {.name = "rtNotifyCreateWithFlag"},
    {.name = "rtNotifyWait"},
    {.name = "rtNotifyWaitWithTimeOut"},
    {.name = "rtCntNotifyCreate"},
    {.name = "rtCntNotifyCreateWithFlag"},
    {.name = "rtCntNotifyRecord"},
    {.name = "rtCntNotifyWaitWithTimeout"},
    {.name = "rtCntNotifyDestroy"},
    {.name = "rtsCntNotifyRecord"},
    {.name = "rtsCntNotifyWaitWithTimeout"},
    /* Other Part */
    {.name = "rtStreamSynchronize"},
#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
    {.name = "rtDeviceSynchronize"},
    {.name = "rtDeviceSynchronizeWithTimeout"},
#endif
    {.name = "rtStreamDestroy"},
    {.name = "rtDestroyStreamForce"},
    {.name = "rtGetDevice"},
    /* Prefill Part */
    {.name = "rtBeginPrefill"},
    {.name = "rtEndPrefill"},
};

#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
void runtime_hook_resolve(rt_hook_enum_t entry)
{
    void *current = __atomic_load_n(&rt_library_entry[entry].func_ptr, __ATOMIC_ACQUIRE);
    if (current != NULL) {
        return;
    }
    void *resolved = dlsym(RTLD_NEXT, rt_library_entry[entry].name);
    if (resolved != NULL) {
        (void)__atomic_compare_exchange_n(&rt_library_entry[entry].func_ptr, &current, resolved,
                                          false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
}
#endif
