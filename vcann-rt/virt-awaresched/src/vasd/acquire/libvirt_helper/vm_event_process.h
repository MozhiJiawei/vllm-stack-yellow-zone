/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef VM_EVENT_PROCESS_H
#define VM_EVENT_PROCESS_H

#include <condition_variable>
#include <queue>

#include "libvirt_helper.h"

namespace vas::sched {
using namespace vas::common;
using namespace vas::sched::acquire;

class VmEventProcess {
public:
    static void Run();
    static void Stop();
    static void RunListener();
    static void Push(const VmEventInfo &vmEventInfo);
    static VasRet Pop(VmEventInfo &vmEventInfo);

private:
    static std::queue<VmEventInfo> vmEventQueue;
    static std::mutex mtx;
    static std::condition_variable eventCv;

    static int EventCallback(virConnectPtr conn, virDomainPtr domain, int event, int detail, void *opaque);
};
} // namespace vas::sched

#endif // VM_EVENT_PROCESS_H
