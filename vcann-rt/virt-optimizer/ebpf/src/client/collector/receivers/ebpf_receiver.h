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

#ifndef DATA_COLLECTOR_H
#define DATA_COLLECTOR_H

#include <atomic>
#include <memory>
#include <thread>

#include "data_struct.h"

class EBPFReceiver {
public:
    void launch();

    void setSamplingInterval(unsigned int newSamplingInterval);

    static EBPFReceiver &getInstance();

    std::shared_ptr<DataTable> dataBuffer{std::make_shared<DataTable>()};

private:
    static constexpr unsigned int SAMPLING_INTERVAL_DEFAULT = 30;

    [[noreturn]] void mainLoop();

    std::atomic<unsigned int> samplingInterval_{SAMPLING_INTERVAL_DEFAULT};

    std::shared_ptr<DataTable> readBuffer_{std::make_shared<DataTable>()};

    std::unique_ptr<std::thread> receiverThread_{nullptr};
};

#endif
