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

#ifndef COLLECTOR_MANAGER
#define COLLECTOR_MANAGER

#include <string>
#include <unordered_map>

#include "rapidjson/document.h"

#include "collector/collector.h"

constexpr const char *PID_FILE_PATH = "/usr/local/sbin/ubs-optimizer/ubs-opt.pid";

class CollectorManager {
public:
    bool loadConfig();

    void mainLoop();

    static CollectorManager &getInstance();

    static void daemonize();

private:
    CollectorManager();

    bool launchCollector(const rapidjson::Document &doc);

    static bool launchReceiver(const rapidjson::Document &doc);

    std::unordered_map<std::string, Collector *> collectors;
};

#endif
