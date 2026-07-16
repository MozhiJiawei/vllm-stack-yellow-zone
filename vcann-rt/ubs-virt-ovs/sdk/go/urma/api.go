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

// Package urma
package urma

import (
	"context"
)

// UbsSetUrmaBandwidth sets the bandwidth range for a given URMA interface in Gbps.
//
// Parameters:
//   - ctx: the context for controlling request lifetime and cancellation
//   - name: the name of the URMA interface to set bandwidth for.
//   - minBandwidth: the minimum bandwidth in Gbps.
//   - maxBandwidth: the maximum bandwidth in Gbps.
//
// Returns:
//   - error: any error encountered during client creation or client call
func UbsSetUrmaBandwidth(
	ctx context.Context,
	name string,
	minBandwidth,
	maxBandwidth uint32,
) error {
	if err := validateUrmaName(name); err != nil {
		return err
	}
	if err := validateUrmaBandwidth(minBandwidth, maxBandwidth); err != nil {
		return err
	}
	cli := newUrmaClientFunc()
	return cli.setBandwidth(ctx, name, minBandwidth, maxBandwidth)
}

// UbsResetUrmaBandwidth resets the bandwidth configuration for a given URMA interface.
//
// Parameters:
//   - ctx: the context for controlling request lifetime and cancellation
//   - name: the name of the URMA interface to reset.
//
// Returns:
//   - error: any error encountered during client creation or client call
func UbsResetUrmaBandwidth(ctx context.Context, name string) error {
	if err := validateUrmaName(name); err != nil {
		return err
	}
	cli := newUrmaClientFunc()
	return cli.resetBandwidth(ctx, name)
}

// UbsGetUrmaBandwidth gets the bandwidth configuration for a given URMA interface.
//
// Parameters:
//   - ctx: the context for controlling request lifetime and cancellation
//   - name: the name of the URMA interface to get bandwidth.
//
// Returns:
//   - uint32: minBandwidth
//   - uint32: maxBandwidth
//   - error: any error encountered during client creation or client call
func UbsGetUrmaBandwidth(ctx context.Context, name string) (uint32, uint32, error) {
	if err := validateUrmaName(name); err != nil {
		return 0, 0, err
	}
	cli := newUrmaClientFunc()
	return cli.getBandwidth(ctx, name)
}
