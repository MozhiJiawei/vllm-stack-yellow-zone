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
	"ubs_virt_ovs_go_sdk/client"
	"ubs_virt_ovs_go_sdk/serde"
)

// BandwidthSetRequest represents a request to configure bandwidth limits for a specific urma entity
type BandwidthSetRequest struct {
	// Name name of urma bonding
	Name string
	// MinBandwidth minimum guaranteed bandwidth in bits per second(Gbps)
	MinBandwidth uint32
	// MaxBandwidth maximum allowed bandwidth in bits per second(Gbps)
	MaxBandwidth uint32
}

// Serialize converts the BandwidthSetRequest into binary format for ipc transmission
func (x *BandwidthSetRequest) Serialize(p *serde.MsgPacker) error {
	if err := p.SerializeString(x.Name); err != nil {
		return err
	}
	if err := p.SerializePod(x.MinBandwidth); err != nil {
		return err
	}
	return p.SerializePod(x.MaxBandwidth)
}

// Deserialize reconstructs the  BandwidthSetRequest from binary data
func (x *BandwidthSetRequest) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	x.Name, err = u.DeserializeString()
	if err != nil {
		return err
	}
	if err = u.DeserializePod(&x.MinBandwidth); err != nil {
		return err
	}
	return u.DeserializePod(&x.MaxBandwidth)
}

// BandwidthResetRequest represents a request to remove bandwidth constraints from an entity
type BandwidthResetRequest struct {
	// Name name of urma bonding
	Name string
}

// Serialize converts the BandwidthResetRequest into binary format
func (x *BandwidthResetRequest) Serialize(p *serde.MsgPacker) error {
	return p.SerializeString(x.Name)
}

// Deserialize reconstructs the BandwidthResetRequest from binary data
func (x *BandwidthResetRequest) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	x.Name, err = u.DeserializeString()
	return err
}

// BandwidthGetRequest represents a request to get bandwidth constraints
type BandwidthGetRequest struct {
	// Name name of urma bonding
	Name string
}

// Serialize converts the BandwidthGetRequest into binary format
func (x *BandwidthGetRequest) Serialize(p *serde.MsgPacker) error {
	return p.SerializeString(x.Name)
}

// Deserialize reconstructs the BandwidthGetRequest from binary data
func (x *BandwidthGetRequest) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	x.Name, err = u.DeserializeString()
	return err
}

// BandwidthGetResponse represents a response for obtaining the bandwidth limit
type BandwidthGetResponse struct {
	client.BaseResponse
	// MinBandwidth minimum guaranteed bandwidth in bits per second(Gbps)
	MinBandwidth uint32
	// MaxBandwidth maximum allowed bandwidth in bits per second(Gbps)
	MaxBandwidth uint32
}

// Serialize converts the BandwidthGetResponse into binary format
func (x *BandwidthGetResponse) Serialize(p *serde.MsgPacker) error {
	if err := p.SerializePod(x.Ret); err != nil {
		return err
	}
	if err := p.SerializeString(x.Message); err != nil {
		return err
	}
	if err := p.SerializePod(x.MinBandwidth); err != nil {
		return err
	}
	return p.SerializePod(x.MaxBandwidth)
}

// Deserialize reconstructs the BandwidthGetResponse from binary data
func (x *BandwidthGetResponse) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	if err = u.DeserializePod(&x.Ret); err != nil {
		return err
	}
	if x.Message, err = u.DeserializeString(); err != nil {
		return err
	}
	if err = u.DeserializePod(&x.MinBandwidth); err != nil {
		return err
	}
	return u.DeserializePod(&x.MaxBandwidth)
}
