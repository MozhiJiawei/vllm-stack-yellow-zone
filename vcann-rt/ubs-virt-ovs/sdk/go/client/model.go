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

// Package client
package client

import "ubs_virt_ovs_go_sdk/serde"

// IpcInfo defines the metadata for an IPC request
type IpcInfo struct {
	// Service name of the target service
	Service string
	// Method name of the method to call
	Method string
}

// Serialize converts the IpcInfo structure into a binary format
func (x *IpcInfo) Serialize(p *serde.MsgPacker) error {
	if err := p.SerializeString(x.Service); err != nil {
		return err
	}
	return p.SerializeString(x.Method)
}

// Deserialize populates the IpcInfo structure from binary data
func (x *IpcInfo) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	x.Service, err = u.DeserializeString()
	if err != nil {
		return err
	}
	x.Method, err = u.DeserializeString()
	return err
}

// IpcRequest represents a complete IPC request message
// Contains both the routing information (IpcInfo) and the actual payload data
type IpcRequest struct {
	// IpcInfo metadata identifying the target service and method
	IpcInfo IpcInfo
	// serialized request data
	Payload []byte
}

// Serialize converts the IpcRequest into a binary format for transmission
func (x *IpcRequest) Serialize(p *serde.MsgPacker) error {
	if err := x.IpcInfo.Serialize(p); err != nil {
		return err
	}
	return p.SerializeBytes(x.Payload)
}

// Deserialize reconstructs the IpcRequest from binary data
func (x *IpcRequest) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	if err = x.IpcInfo.Deserialize(u); err != nil {
		return err
	}
	x.Payload, err = u.DeserializeBytes()
	return err
}

// IpcResponse represents the response from an IPC call
type IpcResponse struct {
	Code    int32
	Payload []byte
}

// Serialize converts the IpcResponse into binary format for transmission
func (x *IpcResponse) Serialize(p *serde.MsgPacker) error {
	if err := p.SerializePod(x.Code); err != nil {
		return err
	}
	return p.SerializeBytes(x.Payload)
}

// Deserialize reconstructs the IpcResponse from binary data
func (x *IpcResponse) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	if err = u.DeserializePod(&x.Code); err != nil {
		return err
	}
	x.Payload, err = u.DeserializeBytes()
	return err
}

// BaseResponse provides a standard response structure with return code and message
type BaseResponse struct {
	Ret     int32
	Message string
}

// Serialize converts the BaseResponse into binary format
func (x *BaseResponse) Serialize(p *serde.MsgPacker) error {
	if err := p.SerializePod(x.Ret); err != nil {
		return err
	}
	return p.SerializeString(x.Message)
}

// Deserialize reconstructs the BaseResponse from binary data
func (x *BaseResponse) Deserialize(u *serde.MsgUnPacker) error {
	var err error
	if err = u.DeserializePod(&x.Ret); err != nil {
		return err
	}
	x.Message, err = u.DeserializeString()
	return err
}
