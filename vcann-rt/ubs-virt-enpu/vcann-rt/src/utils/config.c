/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* ubs-virt-enpu is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/
#include "../include/config.h"
#include <stdlib.h>
#include <string.h>

#define TEN_BASE 10
#define MAX_LINE_LENGTH 256

// Backward compatibility - single device config 
struct Config config = {0};

// New multi-device config support
struct MultiConfig multi_config = {0};
static int current_section_index = -1; // Track which device section we're parsing

void reset_single_config()
{
    config.phy_npu_id = INVALID_VALUE;
    config.vnpu_id = INVALID_VALUE;
    config.aicore_quota = INVALID_VALUE;
    config.memory_quota = INVALID_VALUE;
    config.scheduling_policy = INVALID_VALUE;
    (void)memset_s(config.shm_id, sizeof(config.shm_id), 0, sizeof(config.shm_id));
}

void reset_multi_config()
{
    multi_config.device_count = 0;
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        multi_config.sections[i].section_id = -1;
        multi_config.sections[i].phy_npu_id = INVALID_VALUE;
        multi_config.sections[i].vnpu_id = INVALID_VALUE;
        multi_config.sections[i].aicore_quota = INVALID_VALUE;
        multi_config.sections[i].memory_quota = INVALID_VALUE;
        multi_config.sections[i].scheduling_policy = INVALID_VALUE;
        (void)memset_s(multi_config.sections[i].shm_id, sizeof(multi_config.sections[i].shm_id), 0, sizeof(multi_config.sections[i].shm_id));
    }
}

int check_int32(int32_t option, const char *option_name)
{
    if (option == INVALID_VALUE) {
        LOG_ERROR("\"%s\" is not set. Please check the config and add it as a new line: \"%s=VALUE\"", option_name,
                  option_name);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int check_str(const char *str, const char *option_name)
{
    if (strlen(str) == 0) {
        LOG_ERROR("\"%s\" is not set. Please check the config and add it as a new line: \"%s=VALUE\"", option_name,
                  option_name);
        return ENPU_FAIL;
    }
    return ENPU_SUCCESS;
}

int check_single_config()
{
    return check_int32(config.phy_npu_id, OPTION_NPU_ID) == ENPU_SUCCESS &&
           check_int32(config.vnpu_id, OPTION_VNPU_ID) == ENPU_SUCCESS &&
           check_int32(config.aicore_quota, OPTION_AICORE_QUOTA) == ENPU_SUCCESS &&
           check_int32(config.memory_quota, OPTION_MEMORY_QUOTA) == ENPU_SUCCESS &&
           check_int32(config.scheduling_policy, OPTION_SCHEDULING_POLICY) == ENPU_SUCCESS &&
           check_str(config.shm_id, OPTION_SHM_ID) == ENPU_SUCCESS;
}

int check_multi_config()
{
    if (multi_config.device_count <= 0) {
        LOG_ERROR("No NPU devices configured");
        return ENPU_FAIL;
    }
    
    for (int i = 0; i < multi_config.device_count; i++) {
        struct ConfigSection *sec = &multi_config.sections[i];
        if (sec->section_id < 0 || sec->section_id >= MAX_NPU_DEVICES) {
            LOG_ERROR("Invalid section ID: %d", sec->section_id);
            return ENPU_FAIL;
        }
        
        if (check_int32(sec->phy_npu_id, OPTION_NPU_ID) != ENPU_SUCCESS ||
            check_int32(sec->vnpu_id, OPTION_VNPU_ID) != ENPU_SUCCESS ||
            check_int32(sec->aicore_quota, OPTION_AICORE_QUOTA) != ENPU_SUCCESS ||
            check_int32(sec->memory_quota, OPTION_MEMORY_QUOTA) != ENPU_SUCCESS ||
            check_int32(sec->scheduling_policy, OPTION_SCHEDULING_POLICY) != ENPU_SUCCESS ||
            check_str(sec->shm_id, OPTION_SHM_ID) != ENPU_SUCCESS) {
            LOG_ERROR("Invalid configuration for section [%d]", i);
            return ENPU_FAIL;
        }
    }
    
    return ENPU_SUCCESS;
}

int load_int32(const char *key, const char *value, int32_t *ret_value)
{
    CHECK_COND_RETURN_ERROR_CODE(((key == NULL) || (value == NULL) || (ret_value == NULL)),
                                 "Input para contains NULL!");
    errno = 0;
    char *endptr = NULL;
    long result = strtol(value, &endptr, TEN_BASE);
    CHECK_COND_RETURN_ERROR_CODE(errno != 0, "Failed to load config: %s, value: %s, error message: %s.", key, value,
                                 strerror(errno));
    CHECK_COND_RETURN_ERROR_CODE((endptr == value), "Empty or non-numeric value for config: %s, value: %s.", key,
                                 value);
    CHECK_COND_RETURN_ERROR_CODE((*endptr != '\0'), "Invalid integer for config: %s, value: %s (trailing characters).",
                                 key, value);
    CHECK_COND_RETURN_ERROR_CODE((result < INT32_MIN || result > INT32_MAX),
                                 "Value out of int32 range for config: %s, value: %s.", key, value);
    CHECK_COND_RETURN_ERROR_CODE((result == -1), "Value of config: %s set to -1. This configuration will be ignored.",
                                 key);
    *ret_value = (int32_t)result;
    return ENPU_SUCCESS;
}

int load_str(const char *key, const char *value, char *ret_value, size_t ret_len)
{
    CHECK_COND_RETURN_ERROR_CODE(((key == NULL) || (value == NULL) || (ret_value == NULL)),
                                 "Input para contains NULL!");
    if (strlen(value) > ret_len) {
        LOG_ERROR("Failed to load config: %s, value length (which is %lu)exceed buffer size %zu", key, strlen(value),
                  ret_len);
        return ENPU_FAIL;
    }

    int ret = strcpy_s(ret_value, ret_len, value);
    CHECK_COND_RETURN_ERROR_CODE(ret != 0, "Failed to load config: %s, string copy failed.", key);
    return ENPU_SUCCESS;
}

int save2config_single(const char *key, const char *value)
{
    int rc = ENPU_SUCCESS;
    if (strcmp(key, OPTION_NPU_ID) == 0) {
        rc = load_int32(key, value, &config.phy_npu_id);
    } else if (strcmp(key, OPTION_VNPU_ID) == 0) {
        rc = load_int32(key, value, &config.vnpu_id);
    } else if (strcmp(key, OPTION_AICORE_QUOTA) == 0) {
        rc = load_int32(key, value, &config.aicore_quota);
    } else if (strcmp(key, OPTION_MEMORY_QUOTA) == 0) {
        rc = load_int32(key, value, &config.memory_quota);
    } else if (strcmp(key, OPTION_SCHEDULING_POLICY) == 0) {
        rc = load_int32(key, value, &config.scheduling_policy);
    } else if (strcmp(key, OPTION_SHM_ID) == 0) {
        rc = load_str(key, value, config.shm_id, SHM_ID_LEN);
    } else {
        LOG_WARN("Undefined config key: %s", key);
        rc = ENPU_FAIL;
    }
    return rc;
}

int save2config_section(const char *key, const char *value)
{
    if (current_section_index < 0 || current_section_index >= MAX_NPU_DEVICES) {
        LOG_ERROR("Invalid current section index: %d", current_section_index);
        return ENPU_FAIL;
    }
    
    struct ConfigSection *sec = &multi_config.sections[current_section_index];
    int rc = ENPU_SUCCESS;
    if (strcmp(key, OPTION_NPU_ID) == 0) {
        rc = load_int32(key, value, &sec->phy_npu_id);
    } else if (strcmp(key, OPTION_VNPU_ID) == 0) {
        rc = load_int32(key, value, &sec->vnpu_id);
    } else if (strcmp(key, OPTION_AICORE_QUOTA) == 0) {
        rc = load_int32(key, value, &sec->aicore_quota);
    } else if (strcmp(key, OPTION_MEMORY_QUOTA) == 0) {
        rc = load_int32(key, value, &sec->memory_quota);
    } else if (strcmp(key, OPTION_SCHEDULING_POLICY) == 0) {
        rc = load_int32(key, value, &sec->scheduling_policy);
    } else if (strcmp(key, OPTION_SHM_ID) == 0) {
        rc = load_str(key, value, sec->shm_id, SHM_ID_LEN);
    } else {
        LOG_WARN("Undefined config key: %s", key);
        rc = ENPU_FAIL;
    }
    return rc;
}

// Check if a line is a section header (e.g., [DEVICE-0])
int is_section_header(const char *line)
{
    // Match pattern [DEVICE-n] where n is 0-15
    return (strncmp(line, "[DEVICE-", 8) == 0 && strlen(line) >= 10 && 
            line[8] >= '0' && line[8] <= '9' && line[9] == ']');
}

// Extract section number from header like [DEVICE-0]
int extract_section_number(const char *line)
{
    if (is_section_header(line)) {
        int section_num = atoi(line + 8);  // Convert "0" from "[DEVICE-0]"
        if (section_num >= 0 && section_num < MAX_NPU_DEVICES) {
            return section_num;
        }
    }
    return -1;
}

int load_config(const char *file_path)
{
    static char buffer[MAX_LINE_LENGTH];
    if (!file_path) {
        LOG_ERROR("Invalid input args: file_path=%s", file_path);
        return ENPU_FAIL;
    }

    FILE *file = fopen(file_path, "r");
    CHECK_COND_RETURN_ERROR_CODE(!file, "Failed to open file: %s, error msg: %s.", file_path, strerror(errno));

    // Reset all configurations
    reset_single_config();
    reset_multi_config();
    current_section_index = -1;

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        // Remove whitespace and control characters
        size_t pos = 0;
        size_t len = 0;
        while (buffer[pos] != '\0') {
            if (buffer[pos] < '!') {
                pos += 1;
                continue;
            }
            buffer[len++] = buffer[pos++];
        }
        buffer[len] = '\0';
        
        // Skip comments and empty lines
        if (buffer[0] == '#' || buffer[0] == '\0') {
            continue;
        }

        // Check for section header
        int section_num = extract_section_number(buffer);
        if (section_num >= 0) {
            current_section_index = section_num;
            if (current_section_index < MAX_NPU_DEVICES) {
                // Initialize this section
                multi_config.sections[current_section_index].section_id = current_section_index;
                multi_config.sections[current_section_index].phy_npu_id = INVALID_VALUE;
                multi_config.sections[current_section_index].vnpu_id = INVALID_VALUE;
                multi_config.sections[current_section_index].aicore_quota = INVALID_VALUE;
                multi_config.sections[current_section_index].memory_quota = INVALID_VALUE;
                multi_config.sections[current_section_index].scheduling_policy = INVALID_VALUE;
                (void)memset_s(multi_config.sections[current_section_index].shm_id, 
                               sizeof(multi_config.sections[current_section_index].shm_id), 
                               0, sizeof(multi_config.sections[current_section_index].shm_id));
            }
            continue;
        }

        // Process key-value pairs
        char *equal_pos = strchr(buffer, '=');
        if (!equal_pos) {
            LOG_WARN("Invalid config line format (missing '='): %s", buffer);
            continue;
        }

        *equal_pos = '\0';
        char *key = buffer;
        char *value = equal_pos + 1;

        // Save to appropriate config structure
        int rc;
        if (current_section_index >= 0) {
            // Multi-device mode - save to section
            rc = save2config_section(key, value);
        } else {
            // Single-device mode - save to main config
            rc = save2config_single(key, value);
        }
        
        if (rc == ENPU_SUCCESS) {
            LOG_INFO("Success to load config: %s = %s", key, value);
        } else {
            LOG_WARN("Failed to load config: %s = %s", key, value);
        }
    }

    if (fclose(file) != 0) {
        LOG_ERROR("Failed to close config file. Reason: %s", strerror(errno));
    }
    
    // Determine if this is a multi-device or single-device config
    if (current_section_index >= 0) {
        // Multi-device config - count valid sections
        multi_config.device_count = 0;
        for (int i = 0; i < MAX_NPU_DEVICES; i++) {
            if (multi_config.sections[i].phy_npu_id != INVALID_VALUE) {
                multi_config.device_count++;
            }
        }

        int rc = check_multi_config();
	return rc;
    } else {
        // Single-device config - validate normally
        return check_single_config();
    }
}

int load_multi_config(const char *file_path)
{
    return load_config(file_path);
}

int get_device_count(void)
{
    return multi_config.device_count;
}

int get_device_config(int index, struct ConfigSection *config_out)
{
    if (index < 0 || index >= multi_config.device_count || !config_out) {
        LOG_ERROR("Invalid parameters for get_device_config: index=%d, config_out=%p", 
                 index, config_out);
        return ENPU_FAIL;
    }
    
    // Find the valid section at index
    int valid_count = 0;
    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        if (multi_config.sections[i].phy_npu_id != INVALID_VALUE) {
            if (valid_count == index) {
                *config_out = multi_config.sections[i];
                return ENPU_SUCCESS;
            }
            valid_count++;
        }
    }
    
    return ENPU_FAIL;
}
