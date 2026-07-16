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

#ifndef DATASTRUCT_H
#define DATASTRUCT_H

#include <cstdint>
#include <mutex>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using u64 = unsigned long long;

struct IpiInterrupt {
    uint64_t ipiCount;
    uint64_t transmissionDelay;
    uint64_t processingDelay;

    void clear()
    {
        ipiCount = 0;
        transmissionDelay = 0;
        processingDelay = 0;
    }

    void operator+=(const IpiInterrupt &rhs)
    {
        ipiCount += rhs.ipiCount;
        transmissionDelay += rhs.transmissionDelay;
        processingDelay += rhs.processingDelay;
    }

    bool operator==(const IpiInterrupt &rhs) const
    {
        return (ipiCount == rhs.ipiCount) && (transmissionDelay == rhs.transmissionDelay) &&
               (processingDelay == rhs.processingDelay);
    }

    // Convert IpiInterrupt to JSON
    rapidjson::Value toJson(rapidjson::Document::AllocatorType &allocator) const
    {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("ipi_count", ipiCount, allocator);
        obj.AddMember("transmission_delay", transmissionDelay, allocator);
        obj.AddMember("processing_delay", processingDelay, allocator);
        return obj;
    }

    bool fromJson(const rapidjson::Value &obj)
    {
        if (!obj.IsObject()) {
            return false;
        }

        if (obj.HasMember("ipi_count") && obj["ipi_count"].IsUint64()) {
            ipiCount = obj["ipi_count"].GetUint64();
        } else {
            return false;
        }

        if (obj.HasMember("transmission_delay") && obj["transmission_delay"].IsUint64()) {
            transmissionDelay = obj["transmission_delay"].GetUint64();
        } else {
            return false;
        }

        if (obj.HasMember("processing_delay") && obj["processing_delay"].IsUint64()) {
            processingDelay = obj["processing_delay"].GetUint64();
        } else {
            return false;
        }

        return true;
    }
};

struct SchedSwitch {
    uint64_t contextSwitchCount;
    uint64_t cpuMigrationCount;

    void clear()
    {
        contextSwitchCount = 0;
        cpuMigrationCount = 0;
    }

    void operator+=(const SchedSwitch &rhs)
    {
        contextSwitchCount += rhs.contextSwitchCount;
        cpuMigrationCount += rhs.cpuMigrationCount;
    }

    rapidjson::Value toJson(rapidjson::Document::AllocatorType &allocator) const
    {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("context_switch_count", contextSwitchCount, allocator);
        obj.AddMember("cpu_migration_count", cpuMigrationCount, allocator);
        return obj;
    }
};

struct NumaSwitch {
    uint64_t numaMoveCount;
    uint64_t numaSwapCount;

    void clear()
    {
        numaMoveCount = 0;
        numaSwapCount = 0;
    }

    void operator+=(const NumaSwitch &rhs)
    {
        numaMoveCount += rhs.numaMoveCount;
        numaSwapCount += rhs.numaSwapCount;
    }

    rapidjson::Value toJson(rapidjson::Document::AllocatorType &allocator) const
    {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("numa_move_count", numaMoveCount, allocator);
        obj.AddMember("numa_swap_count", numaSwapCount, allocator);
        return obj;
    }
};

struct DataTable {
    IpiInterrupt ipiInterrupt;
    SchedSwitch schedSwitch;
    NumaSwitch numaSwitch;

    void clear()
    {
        ipiInterrupt.clear();
        schedSwitch.clear();
        numaSwitch.clear();
    }

    // Convert a DataTable to a JSON string
    [[nodiscard]] std::string toJson(const std::string &guestName = "") const
    {
        rapidjson::Document document;
        document.SetObject();
        auto &allocator = document.GetAllocator();

        rapidjson::Value obj(rapidjson::kObjectType);

        // Add attributes here later
        obj.AddMember("ipi_interrupt", ipiInterrupt.toJson(allocator), allocator);
        obj.AddMember("schedule_switch", schedSwitch.toJson(allocator), allocator);
        obj.AddMember("numa_switch", numaSwitch.toJson(allocator), allocator);
        document.AddMember("data_table", obj, allocator);

        std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        document.AddMember("timestamp", timestamp, allocator);

        rapidjson::Value stringVal;
        stringVal.SetString(guestName.c_str(), guestName.size(), allocator);
        document.AddMember("guest_name", stringVal, allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);

        return buffer.GetString();
    }
};

struct MutexContext {
    std::mutex fileMutex;
};

#endif