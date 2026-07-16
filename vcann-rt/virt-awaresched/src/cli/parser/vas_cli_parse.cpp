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

#include "vas_cli_parse.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include <sys/ioctl.h>
#include <unistd.h>

#include "def.h"

namespace vas::cli::framework {

/**
 * @brief Validate Command and Type Length
 *
 * Checks if command and type strings are valid by ensuring:
 * - Neither is empty
 * - Length does not exceed maximum allowed length
 *
 * @param command Command string to validate
 * @param type Type string to validate
 * @return bool True if valid, false otherwise
 */
bool VasCliParse::CheckCommandTypeLength(const std::string &command, const std::string &type)
{
    if (command.empty() || type.empty()) {
        return false;
    } else if (command.length() > MAX_CMD_OR_TYPE_LENGTH || type.length() > MAX_CMD_OR_TYPE_LENGTH) {
        return false;
    }
    return true;
}

/**
 * @brief Check for Duplicate Command Options
 *
 * Verifies if a short or long option already exists in the provided sets
 *
 * @param shortOptions Set of existing short options
 * @param longOptions Set of existing long options
 * @param shortOpt Short option to check
 * @param longOpt Long option to check
 * @return bool True if option is duplicate, false otherwise
 */
bool VasCliParse::CheckDuplicateOptions(const std::unordered_set<std::string> &shortOptions,
                                        const std::unordered_set<std::string> &longOptions, const std::string &shortOpt,
                                        const std::string &longOpt)
{
    if (shortOptions.find(shortOpt) != shortOptions.end()) {
        return true;
    } else if (longOptions.find(longOpt) != longOptions.end()) {
        return true;
    }
    return false;
}

/**
 * @brief Validate Command Options Length
 *
 * Checks if command options are valid by ensuring:
 * - Neither long nor short options are empty
 * - Length of options does not exceed maximum allowed length
 *
 * @param opts Command options information
 * @return bool True if options are valid, false otherwise
 */
bool VasCliParse::CheckOptionsLength(const VasCliSdkOptionsInfo &opts)
{
    if (opts.longOpts.empty() || opts.shortOpts.empty()) {
        return false;
    } else if (opts.longOpts.length() > MAX_OPTIONS_LENGTH || opts.shortOpts.length() > MAX_OPTIONS_LENGTH) {
        return false;
    }
    return true;
}

/**
 * @brief Check Number of Command Options
 *
 * Verifies if the number of options exceeds the maximum allowed limit
 *
 * @param params Vector of command options
 * @return bool True if options number is over limit, false otherwise
 */
bool VasCliParse::CheckOptionsNum(const std::vector<VasCliSdkOptionsInfo> &params)
{
    if (params.size() > MAX_OPTIONS_NUM) {
        return true;
    }

    return false;
}

/**
 * @brief Validate Option Value Length
 *
 * Checks if the option value length is within the maximum allowed limit
 * Prints an error message if length is exceeded
 *
 * @param optionValue Option value to validate
 * @return bool True if value length is valid, false otherwise
 */
bool VasCliParse::CheckOptionValueLength(const std::string &optionValue)
{
    if (optionValue.size() > MAX_VALUE_LENGTH) {
        PrintWithWordWrap("ERROR: The length of the option value has been exceeded " +
                          std::to_string(MAX_VALUE_LENGTH) + ".\n");
        return false;
    }
    return true;
}

/**
 * @brief Register SDK Command Options
 *
 * Processes and validates command options before registration:
 * - Checks option name length
 * - Prevents duplicate short/long options
 * - Validates total number of options
 * - Stores validated command information
 *
 * @param cmdInfo Command information with options to register
 */
void VasCliParse::SetSdkCmdInfoWithOptsMap(const VasCliSdkCmdInfo &cmdInfo)
{
    std::unordered_set<std::string> shortOptionsSet;
    std::unordered_set<std::string> longOptionsSet;
    std::vector<VasCliSdkOptionsInfo> params;
    for (const auto &opts : cmdInfo.params) {
        // Parameter name length (longOpts) should not exceed 32 characters;
        // if it does, an error will be reported and the program will exit.
        if (!CheckOptionsLength(opts)) {
            return;
        }
        // Duplicate option name validation for long and short options; if duplicates are found,
        // do not register them, print an error log, and continue.
        if (CheckDuplicateOptions(shortOptionsSet, longOptionsSet, opts.shortOpts, opts.longOpts)) {
            return;
        }
        shortOptionsSet.insert(opts.shortOpts);
        longOptionsSet.insert(opts.longOpts);
        params.emplace_back(VasCliSdkOptionsInfo{opts.shortOpts, opts.longOpts, opts.desc});
    }

    if (CheckOptionsNum(params)) {
        return;
    }
    // Store a set of option parameters using command and type as unique identifiers.
    std::string key = cmdInfo.command + "_" + cmdInfo.type;
    sdkCmdInfo.emplace_back(
        VasCliSdkCmdInfo{cmdInfo.command, cmdInfo.type, cmdInfo.desc, params, cmdInfo.vasCliSdkCmdFun});
    fullCommand.insert(key);
    sdkCommandWithOptions.insert(std::make_pair(key, params));
}

/**
 * @brief Register SDK Command Information
 *
 * Processes and registers multiple SDK command configurations:
 * - Skips duplicate commands
 * - Validates command and type length
 * - Handles commands with and without options
 *
 * @param regInfo Vector of command information to register
 */
void VasCliParse::SdkCmdInfoRegister(std::vector<VasCliSdkCmdInfo> &regInfo)
{
    for (const auto &item : regInfo) {
        std::string key = item.command + "_" + item.type;
        if (fullCommand.find(key) != fullCommand.end()) {
            continue;
        }
        if (!CheckCommandTypeLength(item.command, item.type)) {
            continue;
        }
        if (!item.params.empty()) {
            SetSdkCmdInfoWithOptsMap(item);
        } else {
            sdkCmdInfo.emplace_back(item);
            fullCommand.insert(key);
        };
    }
}

/**
 * @brief Register SDK command information to the parser instance
 *
 * This function registers SDK command information to the VasCliParse singleton instance.
 * It performs two key pre-validation checks:
 * 1. Checks if the command information vector is non-empty
 * 2. Verifies that the vector size does not exceed the predefined maximum command limit
 *
 * @param vasCliSdkCmdInfo Reference to the vector of SDK command information
 *
 * @note If the vector is empty or its size exceeds MAX_CMD_NUM,
 *       the registration process is skipped
 */
void VasCliParse::VasCliRegisterSdkCmdInfo(std::vector<VasCliSdkCmdInfo> &vasCliSdkCmdInfo)
{
    if (vasCliSdkCmdInfo.empty()) {
        return;
    }

    if (vasCliSdkCmdInfo.size() > MAX_CMD_NUM) {
        return;
    }
    VasCliParse::GetInstance().SdkCmdInfoRegister(vasCliSdkCmdInfo);
}

/**
 * @brief Get Terminal Window Width
 *
 * Retrieves the current width of the terminal window
 * Uses ioctl system call to fetch terminal column size
 *
 * @return size_t Number of columns in the terminal window
 */
size_t VasCliParse::GetTerminalWidth()
{
    struct winsize win {
    };
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == -1) {
        std::cerr << "ERROR: Failed to get terminal size." << std::endl;
    }
    return win.ws_col;
}

/**
 * @brief Display Command Options Help Information
 *
 * Prints detailed help information for command options:
 * - Handles empty parameter set
 * - Formats short and long options with descriptions
 * - Supports line length limit to prevent word wrapping
 *
 * @param params Vector of command options information
 * @param lineLimit Maximum line length for display
 */
void VasCliParse::CommandTypeParamsHelpInfo(const std::vector<VasCliSdkOptionsInfo> &params, const int lineLimit)
{
    if (params.empty()) {
        PrintWithWordWrap("\tThe command does not support any option arguments.");
    }
    for (const auto &param : params) {
        std::stringstream ss;
        // Determine formatting based on terminal width:
        // When terminal width is narrow (<=indentSize+NO_32), use multi-line format with tab indentation
        // When terminal width is sufficient, use single-line format with compact spacing
        if (lineLimit <= indentSize + NO_32) {
            ss << "\t    -" << std::left << std::setw(NO_4) << param.shortOpts << ",--" << std::left << std::setw(NO_32)
               << param.longOpts << "\n\n"
               << std::left << "\t" << param.desc << std::endl;
            std::string currentLine = ss.str();
            PrintWithWordWrap(currentLine);
            std::cout << std::endl;
        } else {
            ss << "    -" << std::left << std::setw(NO_4) << param.shortOpts << ",--" << std::left << std::setw(NO_32)
               << param.longOpts << "  " << std::left << param.desc << std::endl;

            std::string currentLine = ss.str();
            PrintWithLineLimit(currentLine, lineLimit, "\n");
        }
    }
    std::cout << std::endl;
}

/**
 * @brief Display Help Information for Specific Command
 *
 * Retrieves and prints help options for a specific command:
 * - Determines terminal width
 * - Checks if command exists in registered options
 * - Displays command options or error message
 *
 * @param firstCommand Primary command name
 * @param secondCommand Command type or subcommand
 */
void VasCliParse::ParseOneCommandPrtHelpInfo(const std::string &firstCommand, const std::string &secondCommand)
{
    struct winsize win {
    };
    errno = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == -1) {
        std::cerr << "ERROR: Failed to get terminal size., ErrorCode=" << std::to_string(errno) << std::endl;
        return;
    }
    int lineLimit = win.ws_col;
    std::string key = firstCommand + "_" + secondCommand;
    if (sdkCommandWithOptions.find(key) != sdkCommandWithOptions.end()) {
        PrintWithWordWrap("OPTIONS:\n");
        CommandTypeParamsHelpInfo(sdkCommandWithOptions[key], lineLimit);
    } else {
        PrintWithWordWrap("INFO: The command does not exist or does not support parameters.Please try 'vasctl "
                          "--help' for more info.");
    }
}

/**
 * @brief Display Overall Help Information
 *
 * Generates comprehensive help information for all registered commands:
 * - Checks for registered commands
 * - Retrieves terminal width
 * - Prints usage and options for each command
 * - Handles case of no registered commands
 */
void VasCliParse::ParsePrtHelpInfo()
{
    struct winsize win {
    };
    errno = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == -1) {
        std::cerr << "ERROR: Failed to get terminal size., ErrorCode=" << std::to_string(errno) << std::endl;
        return;
    }
    int lineLimit = win.ws_col;
    if (sdkCmdInfo.empty()) {
        PrintWithWordWrap("INFO: No commands have been registered yet.\n");
        return;
    }
    for (const auto &element : sdkCmdInfo) {
        std::string key = element.command + "_" + element.type;
        PrintWithWordWrap("  Usage: " + element.command + " " + element.type + "[OPTIONS]\nOPTIONS:\n");
        CommandTypeParamsHelpInfo(element.params, lineLimit);
    }
}

/**
 * @brief Generate Tab Spacing
 *
 * Calculates and generates appropriate number of spaces to simulate tab alignment
 * Ensures consistent indentation based on current line length
 *
 * @param text Unused input text (for potential future expansion)
 * @param currentLength Current line or text length
 * @return std::string Spaces to add for tab-like alignment
 */
std::string VasCliParse::HandleTab(const std::string &text, size_t currentLength)
{
    (void)text;
    size_t tabWidth = NO_4;
    size_t spacesToAdd = tabWidth - (currentLength % tabWidth);
    return std::string(spacesToAdd, ' ');
}

/**
 * @brief Manage Word Wrapping in Text Line
 *
 * Handles adding words to a line while respecting line length limit:
 * - Prints current line if adding new word would exceed limit
 * - Adds space between words
 * - Manages line continuation
 *
 * @param currentLine Current line being built
 * @param word Word to be added
 * @param lineLimit Maximum allowed line length
 */
void VasCliParse::AddWordToLine(std::string &currentLine, const std::string &word, size_t lineLimit)
{
    if (!currentLine.empty() && currentLine.length() + word.length() + 1 > lineLimit) {
        std::cout << currentLine << std::endl;
        currentLine = word;
    } else {
        if (!currentLine.empty()) {
            currentLine += ' ';
        }
        currentLine += word;
    }
}

/**
 * @brief Print text with word wrapping
 *
 * Prints text while respecting terminal width and handling special characters
 *
 * @param text Input text to be printed
 */
void VasCliParse::PrintWithWordWrap(const std::string &text)
{
    std::string currentLine;
    std::string word;
    size_t lineLimit = GetTerminalWidth();

    for (size_t i = 0; i < text.length(); ++i) {
        char ch = text[i];

        if (ch == '\n') {
            std::cout << currentLine << std::endl;
            currentLine.clear();
        } else if (ch == '\t') {
            currentLine += HandleTab(text, currentLine.length());
        } else if (ch == ' ') {
            if (!word.empty()) {
                AddWordToLine(currentLine, word, lineLimit);
                word.clear();
            }
        } else {
            word += ch;
        }

        if (i == text.length() - 1 || text[i + 1] == ' ' || text[i + 1] == '\n' || text[i + 1] == '\t') {
            if (!word.empty()) {
                AddWordToLine(currentLine, word, lineLimit);
                word.clear();
            }
        }
    }

    if (!currentLine.empty()) {
        std::cout << currentLine << std::endl;
    }
}

void VasCliParse::PrintWithLineLimit(const std::string &str, int lineLimit, const std::string &delimiter) const
{
    int length = int(str.length());          // String length
    int start = 0;                           // Start Marker
    bool isFirstLine = true;                 // Is it the first line?
    int end;                                 // Label at the end of each line
    int step = lineLimit - indentSize;       // Remaining width excluding formatting
    std::string indent(indentSize, ' ');     // Prefix blank line
    std::string prefix = delimiter + indent; // Line Break and Formatting Prefix

    while (start < length) {
        end = std::min(start + (isFirstLine ? lineLimit : step), length);
        isFirstLine = false;

        int lineEnd = end;
        if (lineEnd < length && !std::isspace(str[end])) {
            while (lineEnd > start && !std::isspace(str[lineEnd - 1])) {
                lineEnd--;
            }
            if (lineEnd == start) {
                lineEnd = end;
            }
        }

        std::cout << str.substr(start, lineEnd - start);
        if (lineEnd == length - 1) {
            std::cout << delimiter;
        } else if (lineEnd < length - 1) {
            std::cout << prefix;
        }
        start = lineEnd;

        // Skip whitespace characters to start from the next word.
        while (start < length && std::isspace(str[start])) {
            start++;
        }
    }
}

/**
 * @brief Generate SDK command option parameter map
 *
 * Parses and validates command-line options, checking for long/short options and their values
 *
 * @param args Full list of command-line arguments
 * @param param Current parameter being processed
 * @param cmdOptions Registered options for the current command
 * @param paramIndex Current parameter index (passed by reference to update)
 * @param params Output map to store parsed options
 * @return VasRet Parsing result status
 */
VasRet VasCliParse::GenSdkCmdOptParaMap(const std::vector<std::string> &args, const std::string &param,
                                        const std::vector<VasCliSdkOptionsInfo> &cmdOptions, size_t &paramIndex,
                                        std::map<std::string, std::string> &params)
{
    bool isLongOption;
    // 0 and 2 represent that the first two characters are both "-".
    if (param.length() > NO_2 && param.substr(0, 2) == "--") {
        isLongOption = true;
    } else if (param.length() >= NO_2 && param.substr(0, 1) == "-" && // 0 and 1 represent the first one being -
               param.substr(1, 1) != "-") {                           // 1 and 1 represent the second one that is not -
        isLongOption = false;
    } else {
        PrintWithWordWrap("ERROR: The format of '" + param + "' is invalid.\n");
        return VAS_ERROR;
    }
    std::string optionName = isLongOption ? param.substr(NO_2) : param.substr(NO_1);

    for (const auto &option : cmdOptions) {
        if ((isLongOption && option.longOpts == optionName) || (!isLongOption && option.shortOpts == optionName)) {
            if (params.find(option.longOpts.empty() ? option.shortOpts : option.longOpts) != params.end()) {
                PrintWithWordWrap("ERROR: Duplicate option '" + std::string((isLongOption ? "--" : "-")) + optionName +
                                  "'.\n");
                return VAS_ERROR;
            }
            if (paramIndex + NO_1 >= args.size()) {
                PrintWithWordWrap("ERROR: Option '" + std::string((isLongOption ? "--" : "-")) + optionName +
                                  "' requires a value.\n");
                return VAS_ERROR;
            }
            if (!CheckOptionValueLength(args[paramIndex + NO_1])) {
                return VAS_ERROR;
            }
            params[option.longOpts.empty() ? option.shortOpts : option.longOpts] = args[paramIndex + NO_1];
            paramIndex += NO_2;
            return VAS_OK;
        }
    }
    PrintWithWordWrap("ERROR: Unknown option '" + param + "'.\n");
    return VAS_ERROR;
}

/**
 * @brief Parse SDK command options
 *
 * Validates and processes command-line options for a specific SDK command
 *
 * @param args Full list of command-line arguments
 * @param params Output map to store parsed options
 * @return VasRet Parsing result status
 */
VasRet VasCliParse::SdkCmdOptParse(const std::vector<std::string> &args, std::map<std::string, std::string> &params)
{
    std::string key = args[NO_1] + "_" + args[NO_2];
    // Assume that sdkCommandWithOptions is a registered command option mapping.
    auto cmdIter = sdkCommandWithOptions.find(key);
    if (cmdIter == sdkCommandWithOptions.end()) {
        PrintWithWordWrap("ERROR: The command '" + args[NO_1] + " " + args[NO_2] +
                          "' does not support any long or short options.\n");
        return VAS_ERROR;
    }

    size_t paramIndex = NO_3;
    while (paramIndex < args.size()) {
        const std::string &param = args[paramIndex];
        if (param.empty()) {
            PrintWithWordWrap("ERROR: Unexpected argument '" + param + "'.\n");
            return VAS_ERROR;
        }

        VasRet ret = GenSdkCmdOptParaMap(args, param, cmdIter->second, paramIndex, params);
        if (ret != VAS_OK) {
            return VAS_ERROR;
        }
    }

    return VAS_OK;
}

/**
 * @brief Retrieve registered SDK command information
 *
 * Searches for matching command and type in the registered SDK commands
 *
 * @param args Command-line arguments
 * @return VasRet Command information retrieval status
 */
VasRet VasCliParse::GetSdkRegCmdInfo(const std::vector<std::string> &args)
{
    if (args.size() < NO_3) {
        return VAS_ERROR;
    }
    for (const auto &element : sdkCmdInfo) {
        if ((element.command == args[NO_1]) && (element.type == args[NO_2])) {
            sdkCommandInfo = element;
            return VAS_OK;
        }
    }
    return VAS_ERROR;
}

/**
 * @brief Process help information request
 *
 * Checks various command-line argument scenarios to determine if help should be displayed
 *
 * @param args Command-line arguments
 */
void VasCliParse::PrtHelp(const std::vector<std::string> &args)
{
    if (args.size() <= NO_1) {
        needPrtHelp = true;
        ParsePrtHelpInfo();
        return;
    }
    if (args.size() == NO_2 && (args[NO_1] == "-h" || args[NO_1] == "--help")) {
        needPrtHelp = true;
        ParsePrtHelpInfo();
        return;
    }
    if (args.size() == NO_3 && (args[NO_2] == "-h" || args[NO_2] == "--help")) {
        needPrtHelp = true;
        PrintWithWordWrap("INFO: Double click TAB or query '--help' for more info.");
        return;
    }
    if (args.size() == NO_4 && (args[NO_3] == "-h" || args[NO_3] == "--help")) {
        needPrtHelp = true;
        ParseOneCommandPrtHelpInfo(args[NO_1], args[NO_2]);
        return;
    }
}

/**
 * @brief Parse SDK command-line arguments
 *
 * Handles command-line argument parsing, including help info, command validation, and option parsing
 *
 * @param args List of command-line arguments
 * @return VasRet Parsing result status
 */
VasRet VasCliParse::SdkCliParse(const std::vector<std::string> &args)
{
    PrtHelp(args);
    if (needPrtHelp) {
        return VAS_OK;
    }
    VasRet ret = GetSdkRegCmdInfo(args);
    if (ret != VAS_OK) {
        PrintWithWordWrap("ERROR: Unrecognized command. Please try '--help' for more info.\n");
        return VAS_ERROR;
    }
    // Greater than 3 indicates that there are parameters.
    if (args.size() > NO_3) {
        ret = SdkCmdOptParse(args, inputOptionMap);
        if (ret != VAS_OK) {
            return VAS_ERROR;
        }
    }
    return VAS_OK;
}

/**
 * @brief Get the input option map
 *
 * @return Reference to the input option map
 */
const std::map<std::string, std::string> &VasCliParse::GetInputOptionMap() const
{
    return inputOptionMap;
}

const std::vector<VasCliSdkCmdInfo> &VasCliParse::GetSdkCmdInfo() const
{
    return sdkCmdInfo;
}

const std::unordered_map<std::string, std::vector<VasCliSdkOptionsInfo>> &VasCliParse::GetSdkCommandWithOptions() const
{
    return sdkCommandWithOptions;
}

void VasCliParse::Reset()
{
    sdkCommandInfo = {};
    inputOptionMap.clear();
    sdkCmdInfo.clear();
    fullCommand.clear();
    sdkCommandWithOptions.clear();
    needPrtHelp = false;
}

/**
 * @brief Get the SDK command information
 *
 * @return SDK command information
 */
VasCliSdkCmdInfo VasCliParse::GetSdkCommandInfo()
{
    return sdkCommandInfo;
}
} // namespace vas::cli::framework