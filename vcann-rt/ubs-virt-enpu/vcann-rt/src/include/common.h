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
#ifndef __COMMON_H__
#define __COMMON_H__

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "securec.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define unlikely(x) __builtin_expect(!!(x), 0)
#define MAX_LINE_LENGTH 256

#define ENPU_SUCCESS 0
#define ENPU_FAIL 1

#define CHECK_RETURN_RANGE_INT(val, min, max)                                                                          \
    do {                                                                                                               \
        if (((val) < (min)) || ((val) > (max))) {                                                                      \
            LOG_ERROR("Failed to load param [%s]: [%d] must be in range of [%d] and [%d]", #val, (val), (min), (max)); \
            return ENPU_FAIL;                                                                                          \
        }                                                                                                              \
    } while (false)

/// DO NOT PASS FUNCTION IN
#define CHECK_RETURN_ERROR_CODE(err, error_msg, ...) \
    do {                                             \
        if (unlikely((err) != ENPU_SUCCESS)) {       \
            LOG_ERROR(error_msg, ##__VA_ARGS__);     \
            return (err);                            \
        }                                            \
    } while (false)

#define CHECK_RETURN_ERROR_CODE_LOG(err, error_msg, ...)    \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
            return (err);                                   \
        }                                                   \
    } while (false)

#define CHECK_ERROR_CODE(err, error_msg, ...)    \
    do {                                         \
        if (unlikely((err) != ENPU_SUCCESS)) {   \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
        }                                        \
    } while (false)

#define CHECK_ERROR_CODE_LOG(err, error_msg, ...)           \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_LOG_PRINT(err, error_msg, ...)           \
    do {                                                    \
        if (unlikely((err) != ENPU_SUCCESS)) {              \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
        }                                                   \
    } while (false)

#define CHECK_COND_RETURN(cond, error_msg, ...)  \
    do {                                         \
        if (unlikely(cond)) {                    \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
            return;                              \
        }                                        \
    } while (false)

#define CHECK_COND_RETURN_LOG(cond, error_msg, ...)         \
    do {                                                    \
        if (unlikely(cond)) {                               \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__); \
            return;                                         \
        }                                                   \
    } while (false)

#define CHECK_COND_RETURN_ERROR_CODE(cond, error_msg, ...) \
    do {                                                   \
        if (unlikely(cond)) {                              \
            LOG_ERROR(error_msg, ##__VA_ARGS__);           \
            return ENPU_FAIL;                              \
        }                                                  \
    } while (false)

#define CHECK_COND_RETURN_ERROR_CODE_LOG(cond, error_msg, ...) \
    do {                                                       \
        if (unlikely(cond)) {                                  \
            fprintf(stderr, error_msg "\n", ##__VA_ARGS__);    \
            return ENPU_FAIL;                                  \
        }                                                      \
    } while (false)

#define CHECK_COND_RETURN_(cond, rt, error_msg, ...) \
    do {                                             \
        if (unlikely(cond)) {                        \
            LOG_ERROR(error_msg, ##__VA_ARGS__);     \
            return rt;                               \
        }                                            \
    } while (false)

#define CHECK_COND_LOG_(cond, error_msg, ...)    \
    do {                                         \
        if (unlikely(cond)) {                    \
            LOG_ERROR(error_msg, ##__VA_ARGS__); \
        }                                        \
    } while (false)

extern int get_config_value(const char *file_path, const char *key_name, char *buffer);

#if defined(__cplusplus)
}
#endif

#endif