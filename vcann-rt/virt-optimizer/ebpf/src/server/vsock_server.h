/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef VSOCK_SERVER_H
#define VSOCK_SERVER_H

#include <atomic>
#include <memory>

#include <sys/socket.h>

#include "data_struct.h"

constexpr const char *PID_FILE_PATH = "/usr/local/sbin/ubs-optimizer/ubs-opt-guard.pid";

class VsockServer {
public:
    static void daemonize();

private:
    static int interval;
    static int init();
    static void mainLoop(const int &serverFd, const std::shared_ptr<MutexContext> &context);
};

#endif