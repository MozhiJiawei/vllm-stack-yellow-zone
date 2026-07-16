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

#include <iostream>
#include <utility>

#include "command_parser.h"

CommandParser::CommandParser(std::string name) : name(std::move(name)) {}

void CommandParser::registerHandler(const std::string &cmd, std::function<bool()> handler, const std::string &help)
{
    if (!handler) {
        std::cout << "Error: handler is not set, the command is " + cmd << std::endl;
        return;
    }
    handlers_[cmd] = std::move(handler);
    if (help.empty()) {
        usage.push_back(cmd);
    } else {
        usage.push_back(cmd + " : " + help);
    }
}

bool CommandParser::parse(int argc, char *argv[])
{
    if (argc < ARGC_LEN) {
        printUsage();
        return false;
    }
    if (!argv) {
        std::cout << "Error: argv is null" << std::endl;
        return false;
    }
    const std::string cmd = argv[1];
    if (auto it = handlers_.find(cmd); it != handlers_.end()) {
        return it->second();
    }
    std::cout << "Error: Unknown command '" << cmd << "'" << std::endl;
    printUsage();
    return false;
}

void CommandParser::printUsage()
{
    std::cout << "Usage: " << std::endl;
    for (const auto &cmd : usage) {
        std::cout << "  - " << name << " " << cmd << std::endl;
    }
}
