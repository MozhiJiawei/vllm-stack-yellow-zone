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

#include "ui_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "log/ebpf_logger_macros.h"

constexpr const int MAX_TOTAL_WIDTH = 100;
constexpr const int MIN_COL_WIDTH = 10;
constexpr const int MAX_USER_INPUT = 30;
constexpr const int MAX_USER_ATTEMPT = 3;
constexpr const int COL_WIDTH = 3;
constexpr const int COL_PADDING = 4;
constexpr const int SEPARATOR_EXTRA_SPACE = 2;

class PrettyTable {
public:
    explicit PrettyTable(const std::vector<std::string> &headers)
        : headers_(headers),
          maxTotalWidth_(MAX_TOTAL_WIDTH),
          minColWidth_(MIN_COL_WIDTH),
          colCount_(headers.size())
    {
        // Initialize column width
        colWidths_.resize(colCount_, MIN_COL_WIDTH);
        updateColWidths(headers);
    }

    void addRow(const std::vector<std::string> &rowData)
    {
        if (rowData.size() != static_cast<size_t>(colCount_)) {
            throw std::invalid_argument("Row data size doesn't match header size.");
        }
        rows_.push_back(rowData);
        updateColWidths(rowData);
    }

    void printTable()
    {
        // Calculate the final column width
        auto finalWidths = calculateLayout();

        // Print header
        printSeparator(finalWidths);
        printHeader(finalWidths);
        printSeparator(finalWidths);

        // Print data lines
        for (const auto &row : rows_) {
            printRow(row, finalWidths);
            printSeparator(finalWidths);
        }
    }

private:
    void updateColWidths(const std::vector<std::string> &row)
    {
        for (int i = 0; i < colCount_; i++) {
            // Pure English processing, directly use the string length
            int width = static_cast<int>(row[i].length());
            if (width > colWidths_[i]) {
                colWidths_[i] = std::min(width, maxTotalWidth_);
            }
        }
    }

    std::vector<int> calculateLayout()
    {
        // Calculate table border width
        int borderWidth = COL_WIDTH * (colCount_ - 1) + COL_PADDING;
        int availableWidth = maxTotalWidth_ - borderWidth;

        // Calculate the ideal total width
        int totalIdealWidth = 0;
        for (int w : colWidths_) {
            totalIdealWidth += w;
        }

        // If the total width is less than the available width, return directly
        if (totalIdealWidth <= availableWidth) {
            return colWidths_;
        }

        // Proportionally distribute available width
        std::vector<int> allocatedWidths(colCount_, minColWidth_);
        for (int i = 0; i < colCount_; i++) {
            allocatedWidths[i] = std::max(
                minColWidth_,
                static_cast<int>(std::floor(availableWidth * static_cast<double>(colWidths_[i]) / totalIdealWidth)));
        }

        // Adjust distribution error
        int totalAllocated = 0;
        for (int w : allocatedWidths)
            totalAllocated += w;

        while (totalAllocated > availableWidth) {
            int maxIndex = -1;
            int maxValue = minColWidth_;

            for (int i = 0; i < colCount_; i++) {
                if (allocatedWidths[i] > maxValue && allocatedWidths[i] > minColWidth_) {
                    maxValue = allocatedWidths[i];
                    maxIndex = i;
                }
            }

            if (maxIndex >= 0) {
                allocatedWidths[maxIndex]--;
                totalAllocated--;
            } else {
                break;
            }
        }

        return allocatedWidths;
    }

    std::vector<std::string> wrapText(const std::string &text, int width)
    {
        std::vector<std::string> lines;
        if (text.empty()) {
            lines.emplace_back("");
            return lines;
        }

        std::istringstream outputStr(text);
        std::string word;
        std::string currentLine;

        while (outputStr >> word) {
            // If the word itself exceeds the column width, it needs to be split
            if (static_cast<int>(word.length()) > width) {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }

                // Split very long words
                for (size_t i = 0; i < word.length(); i += static_cast<size_t>(width)) {
                    int chunkSize = std::min(static_cast<int>(word.length() - i), width);
                    lines.push_back(word.substr(i, chunkSize));
                }
            } else if (currentLine.empty()) { // Words can be added to the current line
                currentLine = word;
            } else if (static_cast<int>(currentLine.length() + 1 + word.length()) <=
                       width) { // Check if the column width is exceeded after adding the word
                currentLine += " " + word;
            } else { // Need to wrap
                lines.push_back(currentLine);
                currentLine = word;
            }
        }

        // Add last line
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }

        return lines;
    }

    void printSeparator(const std::vector<int> &widths)
    {
        std::cout << "+";
        for (int w : widths) {
            std::cout << std::string(w + SEPARATOR_EXTRA_SPACE, '-') << "+";
        }
        std::cout << std::endl;
    }

    void printHeader(const std::vector<int> &widths)
    {
        std::vector<std::vector<std::string>> headerLines;

        // Wrap each header text
        for (int i = 0; i < colCount_; i++) {
            headerLines.push_back(wrapText(headers_[i], widths[i]));
        }

        // Calculate the maximum number of rows
        int maxLines = 0;
        for (const auto &lines : headerLines) {
            if (static_cast<int>(lines.size()) > maxLines) {
                maxLines = static_cast<int>(lines.size());
            }
        }

        // Print each row of the header
        for (int lineNum = 0; lineNum < maxLines; lineNum++) {
            std::cout << "|";
            for (int i = 0; i < colCount_; i++) {
                std::string text;
                if (lineNum < static_cast<int>(headerLines[i].size())) {
                    text = headerLines[i][lineNum];
                }

                // Calculate the center position
                int padding = widths[i] - static_cast<int>(text.length());
                int leftPadding = padding / 2;
                int rightPadding = padding - leftPadding;

                std::cout << " " << std::string(leftPadding, ' ') << text << std::string(rightPadding, ' ') << " |";
            }
            std::cout << std::endl;
        }
    }

    void printRow(const std::vector<std::string> &row, const std::vector<int> &widths)
    {
        std::vector<std::vector<std::string>> cellLines;

        // Wrap each cell text
        for (int i = 0; i < colCount_; i++) {
            cellLines.push_back(wrapText(row[i], widths[i]));
        }

        // Calculate how many lines of text need to be printed for this line
        int maxLines = 0;
        for (const auto &lines : cellLines) {
            if (static_cast<int>(lines.size()) > maxLines) {
                maxLines = static_cast<int>(lines.size());
            }
        }

        // Print each line of text
        for (int lineNum = 0; lineNum < maxLines; lineNum++) {
            std::cout << "|";
            for (int i = 0; i < colCount_; i++) {
                std::string text;
                if (lineNum < static_cast<int>(cellLines[i].size())) {
                    text = cellLines[i][lineNum];
                }

                // Left aligned
                std::cout << " " << std::setw(widths[i]) << std::left << text << " |";
            }
            std::cout << std::endl;
        }
    }

private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    int maxTotalWidth_;
    int minColWidth_;
    int colCount_;
    std::vector<int> colWidths_;
};

// Input string processing function
bool processInput(const std::string &input, std::set<size_t> &selection, size_t max)
{
    std::set<size_t> tempSet; // Temporarily store parsing results
    bool valid = true;        // Input validity flag

    std::istringstream iss(input);
    std::string token;

    // Handle empty input (treat it as valid and don't select any number)
    if (input.empty()) {
        return valid;
    }

    // Check first and last characters and input length
    if (input.front() == ',' || input.back() == ',' || input.length() > MAX_USER_INPUT) {
        valid = false;
    }

    // Token-by-token parsing
    while (valid && std::getline(iss, token, ',')) {
        // Check that token is not empty (to prevent consecutive commas)
        if (token.empty()) {
            valid = false;
            break;
        }

        // Check if the token is pure digital
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            break;
        }
        try {
            size_t id = std::stoul(token);
            if (id < 1 || id > max) {
                std::cout << "\nSkip Invalid ID: " << id << "\n";
            } else {
                tempSet.insert(id);
            }
        } catch (...) { // Catch conversion exceptions (such as oversized numbers)
            EBPF_LOG_ERROR("Invalid input format.");
            valid = false;
            break;
        }
    }

    if (valid) {
        // Overall effectiveness: add temporary results to selection
        selection.insert(tempSet.begin(), tempSet.end());
    } else {
        // Invalid as a whole: output error message
        std::cout << "Invalid input format." << std::endl;
    }
    return valid;
}

// User interface processor
void UIManager::displaySuggestions(const std::vector<std::shared_ptr<BaseTuner>> &suggestions) const
{
    std::cout << "\nTotal identified " << suggestions.size() << " optimization option(s):\n";

    // Create table
    PrettyTable table({"ID", "Category", "Description", "Suggestion"});

    // Add optimization suggestions
    for (size_t i = 0; i < suggestions.size(); ++i) {
        const auto &t = suggestions[i];
        if (t->isLastCheckSuccess) {
            table.addRow({std::to_string(i + 1), t->category(), t->principle(), t->advice()});
        } else {
            table.addRow({std::to_string(i + 1), t->category(), t->principle(),
                          "[ERROR] Failed to check. Please refer to the log for more details: "
                          "'/var/ubs-opt/log/ubs_optimizer_tuner.log'."});
        }
    }
    // Print form
    table.printTable();
}

std::set<size_t> UIManager::getUserSelection(size_t max) const
{
    int attempts = 0;
    std::set<size_t> origin;
    while (attempts < MAX_USER_ATTEMPT) {
        std::cout << "Please select the optimization options (enter 'all' to select all or enter numbers like '1,3'): ";
        std::string input;
        std::getline(std::cin, input);

        std::set<size_t> selection;

        // Handling Select All Options
        if (input == "all") {
            for (size_t i = 1; i <= max; ++i) {
                selection.insert(i);
            }
            return selection;
        }
        if (processInput(input, selection, max)) {
            return selection;
        }
        attempts++;
    }
    return origin;
}

void UIManager::executeSelected(const std::vector<std::shared_ptr<BaseTuner>> &suggestions,
                                const std::set<size_t> &selection) const
{
    std::cout << "\nStart Optimization..." << std::endl;
    for (size_t id : selection) {
        if (id <= suggestions.size()) {
            std::cout << "\n[Running Optimizer#" << id << " - " << suggestions[id - 1]->name() << "]\n";
            suggestions[id - 1]->apply();
        } else {
            std::cout << "Skip Invalid ID: " << id << std::endl;
        }
    }
    std::cout << "\nOptimization completed. For more information, please refer to the official documentation."
              << std::endl;
}