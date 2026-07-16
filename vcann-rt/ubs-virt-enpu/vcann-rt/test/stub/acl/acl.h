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

#ifndef ACL_H
#define ACL_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *aclrtStream;
typedef void *aclrtEvent;
typedef void *aclrtContext;
typedef void *aclrtNotify;
typedef void *aclrtCntNotify;
typedef void *aclrtLabel;
typedef void *aclrtLabelList;
typedef void *aclrtMbuf;
typedef int aclError;
typedef uint16_t aclFloat16;
typedef struct aclDataBuffer aclDataBuffer;
typedef void *aclrtAllocatorDesc;
typedef void *aclrtAllocator;
typedef void *aclrtAllocatorBlock;
typedef void *aclrtAllocatorAddr;
typedef void *aclrtTaskGrp;

#define ACL_RT_SUCCESS 0

static const int ACL_ERROR_NONE = 0;
static const int ACL_SUCCESS = 0;

static const int ACL_ERROR_INVALID_PARAM = 100000;
static const int ACL_ERROR_UNINITIALIZE = 100001;
static const int ACL_ERROR_REPEAT_INITIALIZE = 100002;
static const int ACL_ERROR_INVALID_FILE = 100003;
static const int ACL_ERROR_WRITE_FILE = 100004;
static const int ACL_ERROR_INVALID_FILE_SIZE = 100005;
static const int ACL_ERROR_PARSE_FILE = 100006;
static const int ACL_ERROR_FILE_MISSING_ATTR = 100007;
static const int ACL_ERROR_FILE_ATTR_INVALID = 100008;
static const int ACL_ERROR_INVALID_DUMP_CONFIG = 100009;
static const int ACL_ERROR_INVALID_PROFILING_CONFIG = 100010;
static const int ACL_ERROR_INVALID_MODEL_ID = 100011;
static const int ACL_ERROR_DESERIALIZE_MODEL = 100012;
static const int ACL_ERROR_PARSE_MODEL = 100013;
static const int ACL_ERROR_READ_MODEL_FAILURE = 100014;
static const int ACL_ERROR_MODEL_SIZE_INVALID = 100015;
static const int ACL_ERROR_MODEL_MISSING_ATTR = 100016;
static const int ACL_ERROR_MODEL_INPUT_NOT_MATCH = 100017;
static const int ACL_ERROR_MODEL_OUTPUT_NOT_MATCH = 100018;
static const int ACL_ERROR_MODEL_NOT_DYNAMIC = 100019;
static const int ACL_ERROR_OP_TYPE_NOT_MATCH = 100020;
static const int ACL_ERROR_OP_INPUT_NOT_MATCH = 100021;
static const int ACL_ERROR_OP_OUTPUT_NOT_MATCH = 100022;
static const int ACL_ERROR_OP_ATTR_NOT_MATCH = 100023;
static const int ACL_ERROR_OP_NOT_FOUND = 100024;
static const int ACL_ERROR_OP_LOAD_FAILED = 100025;
static const int ACL_ERROR_UNSUPPORTED_DATA_TYPE = 100026;
static const int ACL_ERROR_FORMAT_NOT_MATCH = 100027;
static const int ACL_ERROR_BIN_SELECTOR_NOT_REGISTERED = 100028;
static const int ACL_ERROR_KERNEL_NOT_FOUND = 100029;
static const int ACL_ERROR_BIN_SELECTOR_ALREADY_REGISTERED = 100030;
static const int ACL_ERROR_KERNEL_ALREADY_REGISTERED = 100031;
static const int ACL_ERROR_INVALID_QUEUE_ID = 100032;
static const int ACL_ERROR_REPEAT_SUBSCRIBE = 100033;
static const int ACL_ERROR_STREAM_NOT_SUBSCRIBE = 100034;
static const int ACL_ERROR_THREAD_NOT_SUBSCRIBE = 100035;
static const int ACL_ERROR_WAIT_CALLBACK_TIMEOUT = 100036;
static const int ACL_ERROR_REPEAT_FINALIZE = 100037;
static const int ACL_ERROR_NOT_STATIC_AIPP = 100038;
static const int ACL_ERROR_COMPILING_STUB_MODE = 100039;
static const int ACL_ERROR_GROUP_NOT_SET = 100040;
static const int ACL_ERROR_GROUP_NOT_CREATE = 100041;
static const int ACL_ERROR_PROF_ALREADY_RUN = 100042;
static const int ACL_ERROR_PROF_NOT_RUN = 100043;
static const int ACL_ERROR_DUMP_ALREADY_RUN = 100044;
static const int ACL_ERROR_DUMP_NOT_RUN = 100045;
static const int ACL_ERROR_PROF_REPEAT_SUBSCRIBE = 148046;
static const int ACL_ERROR_PROF_API_CONFLICT = 148047;
static const int ACL_ERROR_INVALID_MAX_OPQUEUE_NUM_CONFIG = 148048;
static const int ACL_ERROR_INVALID_OPP_PATH = 148049;
static const int ACL_ERROR_OP_UNSUPPORTED_DYNAMIC = 148050;
static const int ACL_ERROR_RELATIVE_RESOURCE_NOT_CLEARED = 148051;
static const int ACL_ERROR_UNSUPPORTED_JPEG = 148052;
static const int ACL_ERROR_INVALID_BUNDLE_MODEL_ID = 148053;

static const int ACL_ERROR_BAD_ALLOC = 200000;
static const int ACL_ERROR_API_NOT_SUPPORT = 200001;
static const int ACL_ERROR_INVALID_DEVICE = 200002;
static const int ACL_ERROR_MEMORY_ADDRESS_UNALIGNED = 200003;
static const int ACL_ERROR_RESOURCE_NOT_MATCH = 200004;
static const int ACL_ERROR_INVALID_RESOURCE_HANDLE = 200005;
static const int ACL_ERROR_FEATURE_UNSUPPORTED = 200006;
static const int ACL_ERROR_PROF_MODULES_UNSUPPORTED = 200007;

static const int ACL_ERROR_STORAGE_OVER_LIMIT = 300000;

static const int ACL_ERROR_INTERNAL_ERROR = 500000;
static const int ACL_ERROR_FAILURE = 500001;
static const int ACL_ERROR_GE_FAILURE = 500002;
static const int ACL_ERROR_RT_FAILURE = 500003;
static const int ACL_ERROR_DRV_FAILURE = 500004;
static const int ACL_ERROR_PROFILING_FAILURE = 500005;

const char *aclrtGetSocName();

#if defined(__cplusplus)
}
#endif

#endif // ACL_H