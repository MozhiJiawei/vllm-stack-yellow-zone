/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#ifndef ACL_RT_H
#define ACL_RT_H

#include <acl/acl.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *aclrtBinHandle;
typedef void *aclrtFuncHandle;
typedef void *aclrtArgsHandle;
typedef struct aclrtLaunchKernelCfg aclrtLaunchKernelCfg;
typedef struct aclrtPlaceHolderInfo aclrtPlaceHolderInfo;

aclError aclrtBinaryGetFunction(const aclrtBinHandle binHandle, const char *kernelName,
                                aclrtFuncHandle *funcHandle);
aclError aclrtLaunchKernel(aclrtFuncHandle funcHandle, uint32_t blockDim,
                           const void *argsData, size_t argsSize, aclrtStream stream);
aclError aclrtLaunchKernelWithConfig(aclrtFuncHandle funcHandle, uint32_t blockDim,
                                     aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                                     aclrtArgsHandle argsHandle, void *reserve);
aclError aclrtLaunchKernelV2(aclrtFuncHandle funcHandle, uint32_t blockDim,
                             const void *argsData, size_t argsSize,
                             aclrtLaunchKernelCfg *cfg, aclrtStream stream);
aclError aclrtLaunchKernelWithHostArgs(aclrtFuncHandle funcHandle, uint32_t blockDim,
                                       aclrtStream stream, aclrtLaunchKernelCfg *cfg,
                                       void *hostArgs, size_t argsSize,
                                       aclrtPlaceHolderInfo *placeHolderArray,
                                       size_t placeHolderNum);

#if defined(__cplusplus)
}
#endif

#endif
