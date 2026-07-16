/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-virt-ovs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

// Package main for UrmaSdk example
package main

import (
	"context"
	"fmt"
	"time"
	"ubs_virt_ovs_go_sdk/urma"
)

func exampleUbsSetUrmaBandwidth() {
	fmt.Println("[Example] UbsSetUrmaBandwidth")
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*1)
	defer cancel()

	err := urma.UbsSetUrmaBandwidth(ctx, "urma_1", 1, 1)
	if err != nil {
		fmt.Println("[Example] UbsSetUrmaBandwidth err:", err)
		return
	}
	fmt.Println("[Example] UbsSetUrmaBandwidth success")
}

func exampleUbsResetUrmaBandwidth() {
	fmt.Println("[Example] UbsResetUrmaBandwidth")
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*1)
	defer cancel()
	err := urma.UbsResetUrmaBandwidth(ctx, "urma_1")
	if err != nil {
		fmt.Println("[Example] UbsResetUrmaBandwidth err:", err)
		return
	}
	fmt.Println("[Example] UbsResetUrmaBandwidth success")
}

func exampleUbsGetUrmaBandwidth() {
	fmt.Println("[Example] UbsGetUrmaBandwidth")
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*1)
	defer cancel()
	minBandwidth, maxBandwidth, err := urma.UbsGetUrmaBandwidth(ctx, "urma_1")
	if err != nil {
		fmt.Println("[Example] UbsGetUrmaBandwidth err:", err)
		return
	}
	fmt.Printf("[Example] UbsGetUrmaBandwidth minBandwidth:%d maxBandwidth:%d\n", minBandwidth, maxBandwidth)
	fmt.Println("[Example] UbsGetUrmaBandwidth success")
}

func main() {
	fmt.Println("=== UBS URMA Bandwidth SDK Example ===")
	exampleUbsSetUrmaBandwidth()
	exampleUbsGetUrmaBandwidth()
	exampleUbsResetUrmaBandwidth()
	fmt.Println("=== Example Finished ===")
}
