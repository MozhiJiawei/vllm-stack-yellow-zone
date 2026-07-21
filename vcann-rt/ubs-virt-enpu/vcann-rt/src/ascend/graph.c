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
#include "npu_manager.h"
#include "runtime_hook.h"

RUNTIME_HOOK_DEFINE(rtModelExecute, rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    vcann_trace_record(VCANN_TRACE_RT_MODEL_EXECUTE, stm, mdl, NULL, flag, 0, 0);
    core_limiter(stm, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecute, mdl, stm, flag);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtModelExecuteAsync, rtModel_t mdl, rtStream_t stm, uint32_t flag)
{
    vcann_trace_record(VCANN_TRACE_RT_MODEL_EXECUTE, stm, mdl, NULL, flag, 0, 0);
    core_limiter(stm, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteAsync, mdl, stm, flag);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsModelExecute, rtModel_t mdl, int32_t timeout)
{
    atomic_fetch_add(&hasModelExecuteSync, 1);
    vcann_trace_record(VCANN_TRACE_RT_MODEL_EXECUTE_SYNC, NULL, mdl, NULL, (uint32_t)timeout, 0, 0);
    core_limiter(NULL, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecute, mdl, timeout);
    if (ret == ACL_RT_SUCCESS) {
        atomic_fetch_sub(&hasModelExecuteSync, 1);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtModelExecuteSync, rtModel_t mdl, rtStream_t stm, uint32_t flag, int32_t timeout)
{
    atomic_fetch_add(&hasModelExecuteSync, 1);
    vcann_trace_record(VCANN_TRACE_RT_MODEL_EXECUTE_SYNC, stm, mdl, NULL,
                       ((uint64_t)flag << 32) | (uint32_t)timeout, 0, 0);
    core_limiter(stm, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtModelExecuteSync, mdl, stm, flag, timeout);
    if (ret == ACL_RT_SUCCESS) {
        atomic_fetch_sub(&hasModelExecuteSync, 1);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamBeginCapture, rtStream_t stm, const rtStreamCaptureMode mode)
{
    bool capture = true;
    vcann_trace_record(VCANN_TRACE_STREAM_CAPTURE_BEGIN, stm, NULL, NULL, mode, 0, 0);
    core_limiter(stm, set_stream_capture, &capture);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamBeginCapture, stm, mode);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamEndCapture, rtStream_t stm, rtModel_t *captureMdl)
{
    vcann_trace_record(VCANN_TRACE_STREAM_CAPTURE_END, stm, NULL, NULL, 0, 0, 0);
    core_limiter(stm, NULL, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamEndCapture, stm, captureMdl);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        bool capture = false;
        set_stream_capture(&capture, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsModelExecuteAsync, rtModel_t mdl, rtStream_t stm)
{
    vcann_trace_record(VCANN_TRACE_RT_MODEL_EXECUTE, stm, mdl, NULL, 0, 0, 0);
    core_limiter(stm, NULL, NULL);
    return RUNTIME_HOOK_CALL(rt_library_entry, rtsModelExecuteAsync, mdl, stm);
}
