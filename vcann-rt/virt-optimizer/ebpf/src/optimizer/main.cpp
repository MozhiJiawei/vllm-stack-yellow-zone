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
#include <map>
#include <string>

#include "analyzer/base_analyzer.h"
#include "analyzer/cpu_bound_analyzer.h"
#include "analyzer/io_bound_analyzer.h"
#include "analyzer/irq_anomaly_analyzer.h"
#include "command_parser.h"
#include "log/ebpf_logger.h"
#include "log/ebpf_logger_macros.h"
#include "opt_engine.h"
#include "util/file_reader.h"

const std::string LOG_PATH = "/var/ubs-opt/log/ubs_optimizer_tuner.log";
static const std::uint64_t MIN_DATA_SIZE = 20;

using DataStr = std::vector<std::string>;
namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    std::map<std::string, std::string> options;
    options["-f"] = "/var/ubs-opt/data/data.json";

    try {
        // Check if the file exists
        if (!fs::exists(options["-f"])) {
            std::cout << "There is no collected data available for analysis. " << std::endl;
            return 0;
        }
        // Check whether the content of the file meets the requirements
        if (fs::file_size(options["-f"]) < MIN_DATA_SIZE) {
            std::cout << "The collected files are insufficient to support the analysis. " << std::endl
                      << "Please continue collecting. " << std::endl;
            return 0;
        }
    } catch (const fs::filesystem_error &e) {
        std::cout << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cout << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    FileReader reader(options["-f"]);
    auto data = reader.read();

    CommandParser parser("ubs-opt-tuner");
    parser.registerHandler(
        "start",
        [data] {
            std::cout << "Start performance analysis." << std::endl;

            auto &logger = EbpfLogger::getInstance();
            logger.init(LOG_PATH, EbpfLogger::LogLevel::INFO, true, false);

            // Initialize the analysis engine so that more analyzers can be added
            std::vector<std::unique_ptr<BaseAnalyzer<DataStr>>> analyzers;
            analyzers.push_back(std::make_unique<CPUBoundAnalyzer>());
            analyzers.push_back(std::make_unique<IOBoundAnalyzer>());
            analyzers.push_back(std::make_unique<IRQAnomalyAnalyzer>());

            OPTEngine engine(std::move(analyzers));

            // Run the optimization process
            try {
                engine.run(data);
            } catch (const std::exception &e) {
                EBPF_LOG_ERROR("Start optimizer failed.");
                return 1;
            }
            return 0;
        },
        "Start performance analysis and the optimization process.");

    parser.parse(argc, argv);
    return 0;
}