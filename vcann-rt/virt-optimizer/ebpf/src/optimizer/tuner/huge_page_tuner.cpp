/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * ubs-optimizer is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "huge_page_tuner.h"

#include <cmd_executor.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "log/ebpf_logger_macros.h"
#include "utils.h"

constexpr size_t targetHugepagesize = 2048;
constexpr const char *hugepagesizeKey = "Hugepagesize:";
constexpr const char *hugepagesTotalKey = "HugePages_Total:";

std::string HugePageTuner::name() const
{
    return "Huge Page Configuration";
}

std::string HugePageTuner::category() const
{
    return "IO BOUND";
}

std::string HugePageTuner::principle() const
{
    return "A large number of tlbmiss messages exist in the large-memory VM scenario.";
}

std::string HugePageTuner::advice() const
{
    return "Enable 2M huge page for business operations that use more than 2GB of memory.";
}

bool HugePageTuner::check()
{
    static const char *cmd = R"(grep -E \"Hugepagesize|HugePages_Total\" /proc/meminfo)";
    EBPF_LOG_INFO("Checking huge page tunner.");
    isLastCheckSuccess = true;
    auto hostName = utils::getVmName();
    if (hostName.empty()) {
        return true;
    }
    CmdExecutor executor(hostName);
    auto result = executor.runCommand(cmd);
    if (result.first) {
        std::istringstream iss(result.second);
        std::string line;
        bool foundTotal = false;
        bool foundSize = false;
        size_t pagesNum = 0;
        size_t sizeKB = 0;
        while (std::getline(iss, line)) {
            if (line.find(hugepagesTotalKey) != std::string::npos) {
                std::istringstream totalLine(line);
                std::string tmp;
                totalLine >> tmp >> pagesNum;
                foundTotal = true;
            } else if (line.find(hugepagesizeKey) != std::string::npos) {
                std::istringstream sizeLine(line);
                std::string tmp;
                sizeLine >> tmp >> sizeKB;
                foundSize = true;
            }
        }
        if (foundTotal) {
            if (pagesNum == 0) {
                return true;
            }
            if (foundSize) {
                return (sizeKB != targetHugepagesize);
            }
        }
    } else {
        isLastCheckSuccess = false;
        EBPF_LOG_ERROR("Fail to execute cmd of check Hugepagesize.");
    }
    return true;
}

void HugePageTuner::apply()
{
    std::cout << "1. Execute the command 'vim /etc/default/grub' and append 'default_hugepagesz=2M hugepagesz=2M "
                 "hugepages=pageNums' at the end of the 'GRUB_CMDLINE_LINUX' item, where pageNums should be customized "
                 "to the desired number of large pages to be allocated."
              << std::endl
              << "2. Check whether the virtual machine boots with UEFI or BIOS. If it boots with UEFI, execute "
                 "'grub2-mkconfig -o /boot/efi/EFI/openEuler/grub.cfg' (this command is only applicable to openEuler)"
                 " to make the modified configuration effective; "
                 "if it boots with BIOS, execute 'grub2-mkconfig -o /boot/grub2/grub.cfg' to make the modified "
                 "configuration effective."
              << std::endl
              << "3. Restart the virtual machine, and after restarting, check if the 2M large pages are configured "
                 "effectively by running 'cat /proc/meminfo | grep Hugepagesize'."
              << std::endl
              << "4. During inference, add the environment variable 'export GLIBC_TUNABLES=glibc.malloc.hugetlb=2' "
                 "to enable the optimization of 2M large pages for inference. "
              << std::endl;
}