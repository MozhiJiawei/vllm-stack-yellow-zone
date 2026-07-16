/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "error.h"
#include "vas_cli_parse.h"
#include "vas_cli_process_ctl.h"
#include "vasctl_arg_parse.h"

using namespace vas::cli::reg;
using namespace vas::cli::framework;
using namespace vas::common;
using namespace vas::cli;
int main(int argc, char *argv[])
{
    constexpr int maxCliArgs = 10;
    if (argc > maxCliArgs) {
        VasCliParse::PrintWithWordWrap("INFO: Unsupported number of parameters");
        return static_cast<int>(VAS_ERROR);
    }
    if (argv == nullptr) {
        VasCliParse::PrintWithWordWrap("INFO: System input error.");
        return static_cast<int>(VAS_ERROR);
    }
    RegisterCliModuleSDK();
    const auto ret = VasCliProcessCtl::MainExecuteProcess(argc, argv);
    return static_cast<int>(ret);
}