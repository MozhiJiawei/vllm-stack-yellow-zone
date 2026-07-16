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

#pragma once
#include <string>

#include <sys/socket.h>

#include "data_struct.h"

const int PORT_LOWER_LIMIT = 1024;
const int PORT_UPPER_LIMIT = 49151;

bool sendData(const DataTable &data);

int createAndConnectVsock(uint32_t host_port = 10101);

int getPortBind(void);
#endif