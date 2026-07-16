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

#ifndef HASHMAP_H
#define HASHMAP_H
#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MOD 2654435761UL

typedef struct {
    void *ptr; //  Head capture stream
    bool capture_status;
} MapValue;

typedef struct HashNode {
    void *key;
    MapValue value;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode **buckets;
    size_t capacity;
    size_t size;
} HashMap;

HashMap *hashmap_create(size_t capacity);

int hashmap_put(HashMap *map, void *key, void *ptr, bool capture_status);

int hashmap_get(HashMap *map, void *key, MapValue *value);

int hashmap_get_ptr(HashMap *map, void *key, void **ptr);

int hashmap_get_capture_status(HashMap *map, void *key, bool *capture_status);

int hashmap_remove(HashMap *map, void *key);

int hashmap_contains(HashMap *map, void *key);

size_t hashmap_size(HashMap *map);

void hashmap_destroy(HashMap *map);

#if defined(__cplusplus)
}
#endif

#endif // HASHMAP_H