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

#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <functional>
#include <string>
#include <unordered_map>

const int ARGC_LEN = 2;

class CommandParser {
public:
    void registerHandler(const std::string &cmd, std::function<bool()> handler, const std::string &help);

    bool parse(int argc, char *argv[]);

    void printUsage();

    explicit CommandParser(std::string name = "");

private:
    std::unordered_map<std::string, std::function<bool()>> handlers_;
    std::vector<std::string> usage;
    std::string name;
};

#endif
