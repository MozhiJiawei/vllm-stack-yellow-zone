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

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"sync"
	"time"
	"ubs_virt_ovs_go_sdk/serde"
)

var (
	once         sync.Once
	globalClient *IPCClient
)

const (
	unixSocketPath  = "/run/ubsvirt/ovs.sock"
	defaultPoolSize = 100
	maxIpcRespSize  = 4 * 1024 * 1024
)

// Client represents a client interface for IPC communication over unix domain socket
type Client interface {
	Call(ctx context.Context, req *IpcRequest) (*IpcResponse, error)
}

// IPCClient represents a client for IPC communication over unix domain socket
// It manages a connection pool to handle multiple concurrent request efficiently.
type IPCClient struct {
	pool *connPool
}

// GetClient returns a singleton instance of IPCClient with connection pooling.
// the client is initialized once and reused for all IPC communications.
func GetClient() Client {
	once.Do(func() {
		opts := defaultOptions()
		pool := newConnPool(unixSocketPath, defaultPoolSize, opts.Timeout)
		globalClient = &IPCClient{pool: pool}
	})
	return globalClient
}

// Call executes an IPC request and returns the response.
// It handles serialization/deserialization of messages and connection management.
// Req and Resp are generic types that must implement serde.MsgBase interface.
// Parameters:
//   - ctx: context for request cancellation and timeout
//   - c: IPCClient instance
//   - ipcInfo: IPC metadata for the request
//   - req: request object to be sent
//   - newResp: factory function to create response object
//
// Returns:
//   - resp: deserialized response object
//   - err: error if the call fails
func Call[Req serde.MsgBase, Resp serde.MsgBase](
	ctx context.Context, c Client, ipcInfo IpcInfo, req Req, newResp func() Resp) (resp Resp, err error) {
	payload, err := serde.SerializeMsg(req)
	if err != nil {
		var zero Resp
		return zero, fmt.Errorf("serialize request failed: %w", err)
	}

	callReq := &IpcRequest{
		IpcInfo: ipcInfo,
		Payload: payload,
	}
	callResp, err := c.Call(ctx, callReq)
	if err != nil {
		var zero Resp
		return zero, err
	}
	if callResp.Code != 0 {
		var zero Resp
		return zero, fmt.Errorf("call ipc %v failed,code=%d", ipcInfo, callResp.Code)
	}
	resp, err = serde.DeserializeMsg(callResp.Payload, newResp)
	if err != nil {
		var zero Resp
		return zero, fmt.Errorf("deserialize response failed: %w", err)
	}
	return resp, nil
}

func (c *IPCClient) Call(ctx context.Context, req *IpcRequest) (*IpcResponse, error) {
	conn, err := c.pool.get(ctx)
	if err != nil {
		return nil, err
	}
	defer c.pool.put(conn)
	return conn.call(ctx, req)
}

type connWrapper struct {
	conn       net.Conn
	socketPath string
	timeout    time.Duration
	mu         sync.Mutex
}

func (cw *connWrapper) ensureConn(ctx context.Context) error {
	cw.mu.Lock()
	defer cw.mu.Unlock()

	if cw.conn != nil {
		return nil
	}
	dialer := net.Dialer{}
	conn, err := dialer.DialContext(ctx, "unix", cw.socketPath)
	if err != nil {
		return err
	}
	cw.conn = conn
	return nil
}

func (cw *connWrapper) call(ctx context.Context, req *IpcRequest) (*IpcResponse, error) {
	if err := cw.ensureConn(ctx); err != nil {
		return nil, err
	}
	cw.mu.Lock()
	defer cw.mu.Unlock()
	var err error
	if dl, ok := ctx.Deadline(); ok {
		err = cw.conn.SetDeadline(dl)
	} else if cw.timeout > 0 {
		err = cw.conn.SetDeadline(time.Now().Add(cw.timeout))
	}
	if err != nil {
		return nil, fmt.Errorf("set deadline failed: %w", err)
	}
	data, err := serde.SerializeMsg(req)
	if err != nil {
		return nil, fmt.Errorf("marshal IpcRequest failed,err:%w", err)
	}
	if err = binary.Write(cw.conn, binary.BigEndian, uint32(len(data))); err != nil {
		return nil, fmt.Errorf("write IpcRequest len failed,err:%w", err)
	}
	if _, err := cw.conn.Write(data); err != nil {
		return nil, fmt.Errorf("write IpcRequest data failed,err:%w", err)
	}
	var respLen uint32
	if err := binary.Read(cw.conn, binary.BigEndian, &respLen); err != nil {
		return nil, fmt.Errorf("read IpcResponse len failed,err:%w", err)
	}
	if respLen == 0 || respLen > maxIpcRespSize {
		return nil, fmt.Errorf("invalid IpcResponse len:%d", respLen)
	}
	buf := make([]byte, respLen)
	if _, err := io.ReadFull(cw.conn, buf); err != nil {
		return nil, fmt.Errorf("read IpcResponse failed,err:%w", err)
	}
	resp, err := serde.DeserializeMsg(buf, func() *IpcResponse {
		return &IpcResponse{}
	})
	if err != nil {
		return nil, fmt.Errorf("deserialize IpcResponse failed,err:%w", err)
	}
	return resp, nil
}

func (cw *connWrapper) close() {
	if cw.conn != nil {
		_ = cw.conn.Close()
		cw.conn = nil
	}
}

type connPool struct {
	socketPath string
	timeout    time.Duration
	pool       chan *connWrapper
}

func newConnPool(socketPath string, size int, timeout time.Duration) *connPool {
	p := &connPool{
		socketPath: socketPath,
		timeout:    timeout,
		pool:       make(chan *connWrapper, size),
	}

	for i := 0; i < size; i++ {
		p.pool <- &connWrapper{
			socketPath: socketPath,
			timeout:    timeout,
		}
	}
	return p
}

func (p *connPool) get(ctx context.Context) (*connWrapper, error) {
	select {
	case conn := <-p.pool:
		return conn, nil
	case <-ctx.Done():
		return nil, fmt.Errorf("get connection timeout or cancelled: %w", ctx.Err())
	}
}

func (p *connPool) put(conn *connWrapper) {
	if conn == nil {
		return
	}
	select {
	case p.pool <- conn:
	default:
	}
	conn.close()
}
