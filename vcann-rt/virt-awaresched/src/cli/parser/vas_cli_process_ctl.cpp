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
#include "vas_cli_process_ctl.h"

#include <sys/time.h>
#include <csignal>
#include <iostream>

#include "args_util.h"
#include "def.h"
#include "vas_cli_parse.h"
#include "vas_cli_res_echo.h"

namespace vas::common {
using namespace vas::cli::framework;

/**
 * @brief Handle timeout signal
 *
 * Prints timeout error message and exits the process
 *
 * @param signum Signal number received
 */
void VasCliProcessCtl::SignalHandler(int signum)
{
    if (signum == SIGALRM) {
        VasCliParse::PrintWithWordWrap("ERROR: Timeout " + std::to_string(CLI_TIMEOUT_SECONDS) + "s.\n");
    }
    exit(signum);
}

/**
 * @brief Process command with timeout mechanism
 *
 * Parses CLI arguments and sets a timer to handle command execution
 *
 * @param args Command-line arguments
 * @return VasRet Command execution status
 */
VasRet VasCliProcessCtl::MainExecuteProcess(const int &argc, char *argv[])
{
    const std::vector<std::string> args = ArgsUtil::ArgsToVector(argc, argv);
    VasCliParse::GetInstance().PrtHelp(args);
    if (VasCliParse::GetInstance().needPrtHelp) {
        return VAS_ERROR_CMD;
    }
    VasRet ret = VasCliParse::GetInstance().SdkCliParse(args);
    if (ret != VAS_OK) {
        return ret;
    }
    if (signal(SIGALRM, SignalHandler) == SIG_ERR) {
        std::cout << "Failed to set signal handler for SIGALRM." << std::endl;
        exit(EXIT_FAILURE);
    }
    itimerval timer{};
    timer.it_value.tv_sec = CLI_TIMEOUT_SECONDS;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, nullptr);
    ret = VasCliResEcho::GetInstance().ExecuteCommand();
    if (ret != VAS_OK) {
        setitimer(ITIMER_REAL, nullptr, nullptr);
        return ret;
    }
    setitimer(ITIMER_REAL, nullptr, nullptr);
    return VAS_OK;
}
} // namespace vas::common
