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

#include "gic_tuner.h"

#include <cstdlib>
#include <iostream>

#include "cmd_executor.h"
#include "log/ebpf_logger_macros.h"

std::string GICTuner::name() const
{
    return "GICV4.1 Configuration";
}

std::string GICTuner::category() const
{
    return "IRQ ANOMALY";
}

std::string GICTuner::principle() const
{
    return "The overhead caused by virtualized interrupts leads to slow processing of interrupt messages by the "
           "kworker process, resulting in long operator dispatch latency and CPU-side bubbles."
           "(Kunpeng 920 only supports GICv3. Kunpeng 920B/920C can additionally support GICv4.1.)";
}

std::string GICTuner::advice() const
{
    return "Enbable GIC v4.1 configuration. Support direct injection of virtual interrupts and interrupt pass-through "
           "for vSGIs, eliminating the need for VM exit/entry, which can significantly reduce interrupt response "
           "latency and improve throughput for network/IO-intensive workloads.";
}

bool GICTuner::check()
{
    EBPF_LOG_INFO("Checking GIC tunner.");
    static const char *cmd = "cat /proc/cmdline";
    isLastCheckSuccess = true;
    CmdExecutor executor("");
    auto result = executor.runCommand(cmd);
    if (!result.first) {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Check GIC v4.1 configuration failed.");
        return true;
    }
    // Split parameter string
    std::istringstream iss(result.second);
    std::string kernelArgs;
    while (iss >> kernelArgs) {
        // Check if all parameters "kvm-arm.vgic_v4_enable=1" exist
        if (kernelArgs == "kvm-arm.vgic_v4_enable=1") {
            return false;
        }
    }
    return true;
}

void GICTuner::apply()
{
    std::cout << "1. Navigate to 'BIOS->Advanced->MISC Config->Support SMMU' and set 'Support SMMU' to 'Enabled'."
              << std::endl
              << "2. Go to 'BIOS->Advanced->Processor Configuration->GIC Version' and set 'GIC Version' to '4.1'."
              << std::endl
              << "3. Add the parameter 'kvm-arm.vgic_v4_enable=1' to the system boot configuration file 'grub.cfg' of "
                 "the Host OS."
              << std::endl
              << "4. After rebooting, check the host's dmesg output with 'dmesg|grep GIC'. The following display "
                 "indicates that GIC v4.1 has been successfully enabled: 'kvm [1]: GICv4.1 support enabled'."
              << std::endl
              << "5. On the guest, the following dmesg output indicates that vSGI passthrough is enabled: 'Enabling "
                 "SGIs without active state'. "
              << std::endl;
}
