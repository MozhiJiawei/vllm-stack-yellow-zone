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
#include "rts_kernel.h"
#include "rts_model.h"
#include "rts_stars.h"
#include "runtime_hook.h"

RUNTIME_HOOK_DEFINE(rtKernelLaunch, const void *stubFunc, uint32_t blockDim, void *args, uint32_t argsSize,
                    rtSmDesc_t *smDesc, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunch, stubFunc, blockDim, args, argsSize, smDesc, stm);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithHandle, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const void *kernelInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithHandle, hdl, tilingKey, blockDim, argsInfo, smDesc,
                             stm, kernelInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithHandleV2, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithHandleV2, hdl, tilingKey, blockDim, argsInfo, smDesc,
                             stm, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithFlag, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithFlag, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithFlagV2, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithFlagV2, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchEx, void *args, uint32_t argsSize, uint32_t flags, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchEx, args, argsSize, flags, stm);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchFwk, const char_t *opName, void *args, uint32_t argsSize, uint32_t flags,
                    rtStream_t rtStream)
{
    core_limiter(rtStream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchFwk, opName, args, argsSize, flags, rtStream);
}

RUNTIME_HOOK_DEFINE(rtCpuKernelLaunch, const void *soName, const void *kernelName, uint32_t blockDim, const void *args,
                    uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCpuKernelLaunch, soName, kernelName, blockDim, args, argsSize, smDesc,
                             stm);
}

RUNTIME_HOOK_DEFINE(rtCpuKernelLaunchWithFlag, const void *soName, const void *kernelName, uint32_t blockDim,
                    const rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCpuKernelLaunchWithFlag, soName, kernelName, blockDim, argsInfo,
                             smDesc, stm, flags);
}

RUNTIME_HOOK_DEFINE(rtAicpuKernelLaunchWithFlag, const rtKernelLaunchNames_t *launchNames, uint32_t blockDim,
                    const rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtAicpuKernelLaunchWithFlag, launchNames, blockDim, argsInfo, smDesc,
                             stm, flags);
}

RUNTIME_HOOK_DEFINE(rtAicpuKernelLaunchExWithArgs, const uint32_t kernelType, const char_t *const opName,
                    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc,
                    const rtStream_t stm, const uint32_t flags)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtAicpuKernelLaunchExWithArgs, kernelType, opName, blockDim, argsInfo,
                             smDesc, stm, flags);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandle, rtFuncHandle funcHandle, uint32_t blockDim,
                    rtLaunchArgsHandle argsHandle, rtStream_t stm)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandle, funcHandle, blockDim, argsHandle, stm);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandleV2, rtFuncHandle funcHandle, uint32_t blockDim,
                    rtLaunchArgsHandle argsHandle, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandleV2, funcHandle, blockDim, argsHandle, stm,
                             cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandleV3, rtFuncHandle funcHandle, uint32_t blockDim,
                    const rtArgsEx_t *const argsInfo, rtStream_t stm, const rtTaskCfgInfo_t *const cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandleV3, funcHandle, blockDim, argsInfo, stm,
                             cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtVectorCoreKernelLaunchWithHandle, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtVectorCoreKernelLaunchWithHandle, hdl, tilingKey, blockDim, argsInfo,
                             smDesc, stm, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtVectorCoreKernelLaunch, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtVectorCoreKernelLaunch, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithHostArgs, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, void *hostArgs, uint32_t argsSize, rtPlaceHolderInfo_t *placeHolderArray,
                    uint32_t placeHolderNum)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithHostArgs, funcHandle, numBlocks, stm, cfg, hostArgs,
                             argsSize, placeHolderArray, placeHolderNum);
}

RUNTIME_HOOK_DEFINE(rtsLaunchCpuKernel, const rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    const rtKernelLaunchCfg_t *cfg, rtCpuKernelArgs_t *argsInfo)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchCpuKernel, funcHandle, numBlocks, stm, cfg, argsInfo);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithConfig, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, rtArgsHandle argsHandle, void *reserve)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithConfig, funcHandle, numBlocks, stm, cfg, argsHandle,
                             reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithDevArgs, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, const void *args, uint32_t argsSize, void *reserve)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithDevArgs, funcHandle, numBlocks, stm, cfg, args,
                             argsSize, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchRandomNumTask, const rtRandomNumTaskInfo_t *taskInfo, const rtStream_t stm, void *reserve)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchRandomNumTask, taskInfo, stm, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchReduceAsyncTask, const rtReduceInfo_t *reduceInfo, const rtStream_t stm,
                    const void *reserve)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchReduceAsyncTask, reduceInfo, stm, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchUpdateTask, rtStream_t destStm, uint32_t destTaskId, rtStream_t stm,
                    rtTaskUpdateCfg_t *cfg)
{
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchUpdateTask, destStm, destTaskId, stm, cfg);
}