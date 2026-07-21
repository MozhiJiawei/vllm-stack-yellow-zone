/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */
#ifndef ACL_RT_H
#define ACL_RT_H

#include <acl/acl.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *aclrtBinHandle;
typedef void *aclrtFuncHandle;

aclError aclrtBinaryGetFunction(const aclrtBinHandle binHandle, const char *kernelName,
                                aclrtFuncHandle *funcHandle);

#if defined(__cplusplus)
}
#endif

#endif
