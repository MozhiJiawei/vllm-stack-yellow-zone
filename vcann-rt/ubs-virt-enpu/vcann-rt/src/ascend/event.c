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
#include "npu_manager.h"
#include "rts_event.h"
#include "runtime_hook.h"

RUNTIME_HOOK_DEFINE(rtEventCreate, rtEvent_t *evt)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreate, evt);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsEventCreate, rtEvent_t *evt, uint64_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsEventCreate, evt, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsEventCreateEx, rtEvent_t *evt, uint64_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsEventCreateEx, evt, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventCreateWithFlag, rtEvent_t *evt, uint32_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateWithFlag, evt, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventCreateExWithFlag, rtEvent_t *evt, uint32_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventCreateExWithFlag, evt, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*evt));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamWaitEvent, rtStream_t stm, rtEvent_t evt)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamWaitEvent, stm, evt);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventRecord, rtEvent_t evt, rtStream_t stm)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventRecord, evt, stm);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_record_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventDestroy, rtEvent_t evt)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroy, evt);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_destroy_status((void *)evt);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamDestroy, rtStream_t stm)
{
    core_limiter(stm, remove_stream, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamDestroy, stm);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtDestroyStreamForce, rtStream_t stm)
{
    core_limiter(stm, remove_stream, NULL);
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtDestroyStreamForce, stm);
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsNotifyCreate, rtNotify_t *notify, uint64_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsNotifyCreate, notify, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyRecord, rtNotify_t notify, rtStream_t stm)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyRecord, notify, stm);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_record_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyDestroy, rtNotify_t notify)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyDestroy, notify);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_destroy_status((void *)notify);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsNotifyWaitAndReset, rtNotify_t notify, rtStream_t stm, uint32_t timeout)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsNotifyWaitAndReset, notify, stm, timeout);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtStreamWaitEventWithTimeout, rtStream_t stm, rtEvent_t evt, uint32_t timeout)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtStreamWaitEventWithTimeout, stm, evt, timeout);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)evt, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtEventDestroySync, rtEvent_t evt)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtEventDestroySync, evt);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_destroy_status((void *)evt);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyCreate, int32_t deviceId, rtNotify_t *notify)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyCreate, deviceId, notify);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyCreateWithFlag, int32_t deviceId, rtNotify_t *notify, uint32_t flag)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyCreateWithFlag, deviceId, notify, flag);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*notify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyWait, rtNotify_t notify, rtStream_t stm)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyWait, notify, stm);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtNotifyWaitWithTimeOut, rtNotify_t notify, rtStream_t stm, uint32_t timeOut)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtNotifyWaitWithTimeOut, notify, stm, timeOut);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)notify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyCreate, const int32_t deviceId, rtCntNotify_t *const cntNotify)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyCreate, deviceId, cntNotify);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*cntNotify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyCreateWithFlag, const int32_t deviceId, rtCntNotify_t *const cntNotify,
                    const uint32_t flags)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyCreateWithFlag, deviceId, cntNotify, flags);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_create_status((void *)(*cntNotify));
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyRecord, rtCntNotify_t const inCntNotify, rtStream_t const stm,
                    const rtCntNtyRecordInfo_t *const info)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyRecord, inCntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_record_status((void *)inCntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyWaitWithTimeout, rtCntNotify_t const inCntNotify, rtStream_t const stm,
                    const rtCntNtyWaitInfo_t *const info)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyWaitWithTimeout, inCntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)inCntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtCntNotifyDestroy, rtCntNotify_t const inCntNotify)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtCntNotifyDestroy, inCntNotify);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        set_event_destroy_status((void *)inCntNotify);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsCntNotifyRecord, rtCntNotify_t cntNotify, rtStream_t stm, rtCntNotifyRecordInfo_t *info)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsCntNotifyRecord, cntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_record_status((void *)cntNotify, stm);
    }
    return ret;
}

RUNTIME_HOOK_DEFINE(rtsCntNotifyWaitWithTimeout, rtCntNotify_t cntNotify, rtStream_t stm, rtCntNotifyWaitInfo_t *info)
{
    aclError ret = RUNTIME_HOOK_CALL(rt_library_entry, rtsCntNotifyWaitWithTimeout, cntNotify, stm, info);
    if (ret == ACL_RT_SUCCESS && is_core_limit()) {
        add_stream(stm);
        set_event_wait_status((void *)cntNotify, stm);
    }
    return ret;
}
