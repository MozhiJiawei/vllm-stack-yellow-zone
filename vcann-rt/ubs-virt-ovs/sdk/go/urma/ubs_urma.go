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
	"fmt"
	"ubs_virt_ovs_go_sdk/client"
)

var newUrmaClientFunc = func() *urmaClient {
	return newUrmaClient()
}

const (
	urmaServiceName   = "ubs.urma"
	urmaSetBWMethod   = "SetBandwidth"
	urmaResetBWMethod = "ResetBandwidth"
	urmaGetBWMethod   = "GetBandwidth"

	minUrmaBandwidth uint32 = 1
	maxUrmaBandwidth uint32 = 50
	maxUrmaNameLen          = 31
)

func validateUrmaName(name string) error {
	if len(name) == 0 || len(name) > maxUrmaNameLen {
		return fmt.Errorf("name length must be between 1 and %d", maxUrmaNameLen)
	}
	return nil
}

func validateUrmaBandwidth(min, max uint32) error {
	if min < minUrmaBandwidth || max < minUrmaBandwidth ||
		min > maxUrmaBandwidth || max > maxUrmaBandwidth {
		return fmt.Errorf("bandwidth must be between %d and %d", minUrmaBandwidth, maxUrmaBandwidth)
	}
	if min > max {
		return fmt.Errorf("minBandwidth must be less than or equal to maxBandwidth")
	}
	return nil
}

type urmaClient struct {
	ipc client.Client
}

func newUrmaClient() *urmaClient {
	return &urmaClient{
		ipc: client.GetClient(),
	}
}

func (c *urmaClient) setBandwidth(
	ctx context.Context,
	name string,
	minBandwidth, maxBandwidth uint32,
) error {
	req := &BandwidthSetRequest{
		Name:         name,
		MinBandwidth: minBandwidth,
		MaxBandwidth: maxBandwidth,
	}
	ipcInfo := client.IpcInfo{
		Service: urmaServiceName,
		Method:  urmaSetBWMethod,
	}
	resp, err := client.Call(ctx, c.ipc, ipcInfo, req, func() *client.BaseResponse {
		return &client.BaseResponse{}
	})
	if err != nil {
		return err
	}
	if resp.Ret != 0 {
		return fmt.Errorf("setBandwidth %s failed,errCode=%d,reason=%s", name, resp.Ret, resp.Message)
	}
	return nil
}

func (c *urmaClient) resetBandwidth(ctx context.Context, name string) error {
	req := &BandwidthResetRequest{
		Name: name,
	}
	ipcInfo := client.IpcInfo{
		Service: urmaServiceName,
		Method:  urmaResetBWMethod,
	}
	resp, err := client.Call(ctx, c.ipc, ipcInfo, req, func() *client.BaseResponse {
		return &client.BaseResponse{}
	})
	if err != nil {
		return err
	}
	if resp.Ret != 0 {
		return fmt.Errorf("resetBandwidth %s failed,errCode=%d,reason=%s", name, resp.Ret, resp.Message)
	}
	return nil
}

func (c *urmaClient) getBandwidth(ctx context.Context, name string) (uint32, uint32, error) {
	req := &BandwidthResetRequest{
		Name: name,
	}
	ipcInfo := client.IpcInfo{
		Service: urmaServiceName,
		Method:  urmaGetBWMethod,
	}
	resp, err := client.Call(ctx, c.ipc, ipcInfo, req, func() *BandwidthGetResponse {
		return &BandwidthGetResponse{}
	})
	if err != nil {
		return 0, 0, err
	}
	if resp.Ret != 0 {
		return 0, 0, fmt.Errorf("getBandwidth %s failed,errCode=%d,reason=%s", name, resp.Ret, resp.Message)
	}
	return resp.MinBandwidth, resp.MaxBandwidth, nil
}
