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

// Package serde
package serde

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

// MsgBase defines the interface for serializable message types
type MsgBase interface {
	// Serialize converts the message to binary format
	Serialize(p *MsgPacker) error
	// Deserialize reconstructs thr message from binary data
	Deserialize(u *MsgUnPacker) error
}

// MsgPacker provides binary serialization capabilities for message structures
type MsgPacker struct {
	buf *bytes.Buffer
}

// NewMsgPacker create a new message packer with an empty buffer
func NewMsgPacker() *MsgPacker {
	return &MsgPacker{buf: new(bytes.Buffer)}
}

// SerializePod encodes a primitive data type (POD -Plain Old Data) using little-endian byte order
// Supports basic types like int,float,bool,and other fixed-size types
func (p *MsgPacker) SerializePod(data interface{}) error {
	return binary.Write(p.buf, binary.LittleEndian, data)
}

// SerializeString encodes a string with length prefix
// Format: [4-byte length][string bytes]
func (p *MsgPacker) SerializeString(s string) error {
	size := uint32(len(s))
	if err := binary.Write(p.buf, binary.LittleEndian, &size); err != nil {
		return err
	}
	_, err := p.buf.Write([]byte(s))
	return err
}

// SerializeBytes encodes a byte slice with length prefix
// Format: [4-byte length][byte data]
func (p *MsgPacker) SerializeBytes(data []byte) error {
	l := uint32(len(data))
	if err := binary.Write(p.buf, binary.LittleEndian, &l); err != nil {
		return err
	}
	_, err := p.buf.Write(data)
	return err
}

// SerializeVector encodes a slice of arbitrary type with length prefix
func SerializeVector[T any](p *MsgPacker, vec []T, fn func(*MsgPacker, T) error) error {
	size := uint64(len(vec))
	if err := binary.Write(p.buf, binary.LittleEndian, &size); err != nil {
		return err
	}
	for _, v := range vec {
		if err := fn(p, v); err != nil {
			return err
		}
	}
	return nil
}

// SerializeMap encodes a map with length prefix and key-value pairs
func SerializeMap[K comparable, V any](p *MsgPacker, m map[K]V,
	fnK func(*MsgPacker, K) error,
	fnV func(*MsgPacker, V) error) error {
	size := uint64(len(m))
	if err := binary.Write(p.buf, binary.LittleEndian, &size); err != nil {
		return err
	}
	for k, v := range m {
		if err := fnK(p, k); err != nil {
			return err
		}
		if err := fnV(p, v); err != nil {
			return err
		}
	}
	return nil
}

// Bytes returns the serialized binary data as a byte slice
func (p *MsgPacker) Bytes() []byte {
	return p.buf.Bytes()
}

// MsgUnPacker provides binary deserialization capabilities
// Reconstructs data structures from binary format created by MsgPacker
type MsgUnPacker struct {
	buf *bytes.Reader
}

// NewMsgUnPacker creates a new unpacker for the given binary data
func NewMsgUnPacker(data []byte) *MsgUnPacker {
	return &MsgUnPacker{buf: bytes.NewReader(data)}
}

// Remaining returns the number of unread bytes in the buffer
func (u *MsgUnPacker) Remaining() int {
	return u.buf.Len()
}

// DeserializePod decodes a primitive data type using little-endian byte order
func (u *MsgUnPacker) DeserializePod(data interface{}) error {
	return binary.Read(u.buf, binary.LittleEndian, data)
}

// DeserializeString decodes a length-prefixed string
func (u *MsgUnPacker) DeserializeString() (string, error) {
	var size uint32
	if err := binary.Read(u.buf, binary.LittleEndian, &size); err != nil {
		return "", err
	}
	buf := make([]byte, size)
	if _, err := io.ReadFull(u.buf, buf); err != nil {
		return "", err
	}
	return string(buf), nil
}

// DeserializeBytes decodes a length-prefixed byte slice
func (u *MsgUnPacker) DeserializeBytes() ([]byte, error) {
	var size uint32
	if err := u.DeserializePod(&size); err != nil {
		return nil, err
	}
	buf := make([]byte, size)
	if _, err := io.ReadFull(u.buf, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

// DeSerializeVector decode a length-prefixed vector/slice
func DeSerializeVector[T any](u *MsgUnPacker, fn func(*MsgUnPacker) (T, error)) ([]T, error) {
	var size uint64
	if err := binary.Read(u.buf, binary.LittleEndian, &size); err != nil {
		return nil, err
	}
	res := make([]T, 0, size)
	for i := uint64(0); i < size; i++ {
		v, err := fn(u)
		if err != nil {
			return nil, err
		}
		res = append(res, v)
	}
	return res, nil
}

// DeSerializeMap decodes a length-prefixed map with key-value pairs
func DeSerializeMap[K comparable, V any](u *MsgUnPacker,
	fnK func(*MsgUnPacker) (K, error),
	funV func(*MsgUnPacker) (V, error)) (map[K]V, error) {
	var size uint64
	if err := binary.Read(u.buf, binary.LittleEndian, &size); err != nil {
		return nil, err
	}
	res := make(map[K]V, size)
	for i := uint64(0); i < size; i++ {
		k, err := fnK(u)
		if err != nil {
			return nil, err
		}
		v, err := funV(u)
		if err != nil {
			return nil, err
		}
		res[k] = v
	}
	return res, nil
}

// DeserializeMsg reconstructs a message object from binary data using a factory function
func DeserializeMsg[T MsgBase](buf []byte, factory func() T) (T, error) {
	var zero T
	if len(buf) == 0 {
		return zero, errors.New("DeSerializeMsg:empty buffer")
	}
	unpacker := NewMsgUnPacker(buf)
	obj := factory()
	if err := obj.Deserialize(unpacker); err != nil {
		return zero, fmt.Errorf("DeSerializeMsg:deserialize failed,err=%v", err)
	}
	return obj, nil
}

// SerializeMsg converts a message object to binary format
func SerializeMsg(msg MsgBase) ([]byte, error) {
	if msg == nil {
		return nil, errors.New("SerializeMsg:msg is nil")
	}
	packer := NewMsgPacker()
	if err := msg.Serialize(packer); err != nil {
		return nil, fmt.Errorf("SerializeMsg:serialize failed,err=%v", err)
	}
	return packer.Bytes(), nil
}
