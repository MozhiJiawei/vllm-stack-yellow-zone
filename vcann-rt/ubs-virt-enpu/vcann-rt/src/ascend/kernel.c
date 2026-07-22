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
#include "deadlock_trace.h"
#include "log.h"
#include "rts_kernel.h"
#include "rts_model.h"
#include "rts_stars.h"
#include "runtime_hook.h"

#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
#include <acl/acl_rt.h>
#endif

RUNTIME_HOOK_DEFINE(rtKernelLaunch, const void *stubFunc, uint32_t blockDim, void *args, uint32_t argsSize,
                    rtSmDesc_t *smDesc, rtStream_t stm)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_LAUNCH, stm, stubFunc, args, 0, blockDim, argsSize);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunch, stubFunc, blockDim, args, argsSize, smDesc, stm);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithHandle, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const void *kernelInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_HANDLE, stm, hdl, kernelInfo, tilingKey, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithHandle, hdl, tilingKey, blockDim, argsInfo, smDesc,
                             stm, kernelInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithHandleV2, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_HANDLE_V2, stm, hdl, argsInfo, tilingKey, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithHandleV2, hdl, tilingKey, blockDim, argsInfo, smDesc,
                             stm, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithFlag, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_FLAG, stm, stubFunc, argsInfo, flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithFlag, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchWithFlagV2, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_FLAG_V2, stm, stubFunc, argsInfo, flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchWithFlagV2, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchEx, void *args, uint32_t argsSize, uint32_t flags, rtStream_t stm)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_EX, stm, NULL, args, flags, 0, argsSize);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchEx, args, argsSize, flags, stm);
}

RUNTIME_HOOK_DEFINE(rtKernelLaunchFwk, const char_t *opName, void *args, uint32_t argsSize, uint32_t flags,
                    rtStream_t rtStream)
{
    vcann_trace_record(VCANN_TRACE_RT_KERNEL_FWK, rtStream, opName, args, flags, 0, argsSize);
    core_limiter(rtStream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtKernelLaunchFwk, opName, args, argsSize, flags, rtStream);
}

RUNTIME_HOOK_DEFINE(rtCpuKernelLaunch, const void *soName, const void *kernelName, uint32_t blockDim, const void *args,
                    uint32_t argsSize, rtSmDesc_t *smDesc, rtStream_t stm)
{
    vcann_trace_record(VCANN_TRACE_RT_CPU_KERNEL, stm, kernelName, soName, 0, blockDim, argsSize);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCpuKernelLaunch, soName, kernelName, blockDim, args, argsSize, smDesc,
                             stm);
}

RUNTIME_HOOK_DEFINE(rtCpuKernelLaunchWithFlag, const void *soName, const void *kernelName, uint32_t blockDim,
                    const rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    vcann_trace_record(VCANN_TRACE_RT_CPU_KERNEL, stm, kernelName, soName, flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtCpuKernelLaunchWithFlag, soName, kernelName, blockDim, argsInfo,
                             smDesc, stm, flags);
}

RUNTIME_HOOK_DEFINE(rtAicpuKernelLaunchWithFlag, const rtKernelLaunchNames_t *launchNames, uint32_t blockDim,
                    const rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags)
{
    vcann_trace_record(VCANN_TRACE_RT_AICPU_KERNEL, stm, launchNames, argsInfo, flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtAicpuKernelLaunchWithFlag, launchNames, blockDim, argsInfo, smDesc,
                             stm, flags);
}

RUNTIME_HOOK_DEFINE(rtAicpuKernelLaunchExWithArgs, const uint32_t kernelType, const char_t *const opName,
                    const uint32_t blockDim, const rtAicpuArgsEx_t *argsInfo, rtSmDesc_t *const smDesc,
                    const rtStream_t stm, const uint32_t flags)
{
    vcann_trace_record(VCANN_TRACE_RT_AICPU_KERNEL_EX, stm, opName, argsInfo,
                       ((uint64_t)kernelType << 32) | flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtAicpuKernelLaunchExWithArgs, kernelType, opName, blockDim, argsInfo,
                             smDesc, stm, flags);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandle, rtFuncHandle funcHandle, uint32_t blockDim,
                    rtLaunchArgsHandle argsHandle, rtStream_t stm)
{
    vcann_trace_record(VCANN_TRACE_RT_FUNC_HANDLE, stm, funcHandle, argsHandle, 0, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandle, funcHandle, blockDim, argsHandle, stm);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandleV2, rtFuncHandle funcHandle, uint32_t blockDim,
                    rtLaunchArgsHandle argsHandle, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_FUNC_HANDLE_V2, stm, funcHandle, argsHandle, 0, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandleV2, funcHandle, blockDim, argsHandle, stm,
                             cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtLaunchKernelByFuncHandleV3, rtFuncHandle funcHandle, uint32_t blockDim,
                    const rtArgsEx_t *const argsInfo, rtStream_t stm, const rtTaskCfgInfo_t *const cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_FUNC_HANDLE_V3, stm, funcHandle, argsInfo, 0, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtLaunchKernelByFuncHandleV3, funcHandle, blockDim, argsInfo, stm,
                             cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtVectorCoreKernelLaunchWithHandle, void *hdl, const uint64_t tilingKey, uint32_t blockDim,
                    rtArgsEx_t *argsInfo, rtSmDesc_t *smDesc, rtStream_t stm, const rtTaskCfgInfo_t *cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_VECTOR_HANDLE, stm, hdl, argsInfo, tilingKey, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtVectorCoreKernelLaunchWithHandle, hdl, tilingKey, blockDim, argsInfo,
                             smDesc, stm, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtVectorCoreKernelLaunch, const void *stubFunc, uint32_t blockDim, rtArgsEx_t *argsInfo,
                    rtSmDesc_t *smDesc, rtStream_t stm, uint32_t flags, const rtTaskCfgInfo_t *cfgInfo)
{
    vcann_trace_record(VCANN_TRACE_RT_VECTOR_KERNEL, stm, stubFunc, argsInfo, flags, blockDim, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtVectorCoreKernelLaunch, stubFunc, blockDim, argsInfo, smDesc, stm,
                             flags, cfgInfo);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithHostArgs, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, void *hostArgs, uint32_t argsSize, rtPlaceHolderInfo_t *placeHolderArray,
                    uint32_t placeHolderNum)
{
    vcann_trace_record(VCANN_TRACE_RTS_KERNEL_HOST_ARGS, stm, funcHandle, hostArgs, placeHolderNum, numBlocks,
                       argsSize);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithHostArgs, funcHandle, numBlocks, stm, cfg, hostArgs,
                             argsSize, placeHolderArray, placeHolderNum);
}

RUNTIME_HOOK_DEFINE(rtsLaunchCpuKernel, const rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    const rtKernelLaunchCfg_t *cfg, rtCpuKernelArgs_t *argsInfo)
{
    vcann_trace_record(VCANN_TRACE_RTS_CPU_KERNEL, stm, funcHandle, argsInfo, 0, numBlocks, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchCpuKernel, funcHandle, numBlocks, stm, cfg, argsInfo);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithConfig, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, rtArgsHandle argsHandle, void *reserve)
{
    vcann_trace_record(VCANN_TRACE_RTS_KERNEL_CONFIG, stm, funcHandle, argsHandle, 0, numBlocks, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithConfig, funcHandle, numBlocks, stm, cfg, argsHandle,
                             reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchKernelWithDevArgs, rtFuncHandle funcHandle, uint32_t numBlocks, rtStream_t stm,
                    rtKernelLaunchCfg_t *cfg, const void *args, uint32_t argsSize, void *reserve)
{
    vcann_trace_record(VCANN_TRACE_RTS_KERNEL_DEV_ARGS, stm, funcHandle, args, 0, numBlocks, argsSize);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchKernelWithDevArgs, funcHandle, numBlocks, stm, cfg, args,
                             argsSize, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchRandomNumTask, const rtRandomNumTaskInfo_t *taskInfo, const rtStream_t stm, void *reserve)
{
    vcann_trace_record(VCANN_TRACE_RTS_RANDOM_TASK, stm, taskInfo, reserve, 0, 0, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchRandomNumTask, taskInfo, stm, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchReduceAsyncTask, const rtReduceInfo_t *reduceInfo, const rtStream_t stm,
                    const void *reserve)
{
    vcann_trace_record(VCANN_TRACE_RTS_REDUCE_TASK, stm, reduceInfo, reserve, 0, 0, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchReduceAsyncTask, reduceInfo, stm, reserve);
}

RUNTIME_HOOK_DEFINE(rtsLaunchUpdateTask, rtStream_t destStm, uint32_t destTaskId, rtStream_t stm,
                    rtTaskUpdateCfg_t *cfg)
{
    vcann_trace_record(VCANN_TRACE_RTS_UPDATE_TASK, stm, destStm, cfg, destTaskId, 0, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsLaunchUpdateTask, destStm, destTaskId, stm, cfg);
}

#ifdef VCANN_ENABLE_DEADLOCK_DIAGNOSTICS
/*
 * CANN's aclnn path can acquire a named function handle through AscendCL and
 * launch it later through rtLaunchKernelByFuncHandle*. That path does not
 * necessarily call rtFunctionRegister, so retain the name at the ACL boundary
 * where both the stable name and the returned launch handle are available.
 */
__attribute__((visibility("default"))) aclError aclrtBinaryGetFunction(
    const aclrtBinHandle binHandle, const char *kernelName, aclrtFuncHandle *funcHandle)
{
    runtime_hook_resolve(HOOK_aclrtBinaryGetFunction);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, aclrtBinaryGetFunction, binHandle,
                                     kernelName, funcHandle);
    if (ret == ACL_SUCCESS && funcHandle != NULL && *funcHandle != NULL && kernelName != NULL) {
        vcann_trace_kernel_map_handle(*funcHandle, binHandle, kernelName);
    }
    return ret;
}

__attribute__((visibility("default"))) aclError aclrtLaunchKernel(
    aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize,
    aclrtStream stream)
{
    runtime_hook_resolve(HOOK_aclrtLaunchKernel);
    vcann_trace_record(VCANN_TRACE_ACL_KERNEL, (rtStream_t)stream, funcHandle, argsData,
                       0, blockDim, (uint32_t)argsSize);
    (void)det_sched_gate((rtStream_t)stream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtLaunchKernel, funcHandle, blockDim,
                             argsData, argsSize, stream);
}

__attribute__((visibility("default"))) aclError aclrtLaunchKernelWithConfig(
    aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
    aclrtLaunchKernelCfg *cfg, aclrtArgsHandle argsHandle, void *reserve)
{
    runtime_hook_resolve(HOOK_aclrtLaunchKernelWithConfig);
    vcann_trace_record(VCANN_TRACE_ACL_KERNEL_CONFIG, (rtStream_t)stream, funcHandle,
                       argsHandle, 0, blockDim, 0);
    (void)det_sched_gate((rtStream_t)stream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtLaunchKernelWithConfig, funcHandle,
                             blockDim, stream, cfg, argsHandle, reserve);
}

__attribute__((visibility("default"))) aclError aclrtLaunchKernelV2(
    aclrtFuncHandle funcHandle, uint32_t blockDim, const void *argsData, size_t argsSize,
    aclrtLaunchKernelCfg *cfg, aclrtStream stream)
{
    runtime_hook_resolve(HOOK_aclrtLaunchKernelV2);
    vcann_trace_record(VCANN_TRACE_ACL_KERNEL_V2, (rtStream_t)stream, funcHandle, argsData,
                       0, blockDim, (uint32_t)argsSize);
    (void)det_sched_gate((rtStream_t)stream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtLaunchKernelV2, funcHandle, blockDim,
                             argsData, argsSize, cfg, stream);
}

__attribute__((visibility("default"))) aclError aclrtLaunchKernelWithHostArgs(
    aclrtFuncHandle funcHandle, uint32_t blockDim, aclrtStream stream,
    aclrtLaunchKernelCfg *cfg, void *hostArgs, size_t argsSize,
    aclrtPlaceHolderInfo *placeHolderArray, size_t placeHolderNum)
{
    runtime_hook_resolve(HOOK_aclrtLaunchKernelWithHostArgs);
    vcann_trace_record(VCANN_TRACE_ACL_KERNEL_HOST_ARGS, (rtStream_t)stream, funcHandle,
                       hostArgs, placeHolderNum, blockDim, (uint32_t)argsSize);
    (void)det_sched_gate((rtStream_t)stream, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, aclrtLaunchKernelWithHostArgs, funcHandle,
                             blockDim, stream, cfg, hostArgs, argsSize, placeHolderArray,
                             placeHolderNum);
}

RUNTIME_HOOK_DEFINE(rtFunctionRegister, void *binHandle, const void *stubFunc, const char *stubName,
                    const void *devFunc, uint32_t funcMode)
{
    runtime_hook_resolve(HOOK_rtFunctionRegister);
    rtError_t ret = RUNTIME_HOOK_CALL(rt_library_entry, rtFunctionRegister, binHandle, stubFunc,
                                      stubName, devFunc, funcMode);
    if (ret == RT_ERROR_NONE) {
        vcann_trace_kernel_register(binHandle, stubFunc, stubName, devFunc, funcMode);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDevBinaryUnRegister, void *binHandle)
{
    runtime_hook_resolve(HOOK_rtDevBinaryUnRegister);
    rtError_t ret = RUNTIME_HOOK_CALL(rt_library_entry, rtDevBinaryUnRegister, binHandle);
    if (ret == RT_ERROR_NONE) {
        vcann_trace_kernel_unregister(binHandle);
    }
    return ret;
}
#endif
