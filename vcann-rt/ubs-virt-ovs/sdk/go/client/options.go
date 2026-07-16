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

import "time"

var (
	defaultTimeout = 5 * time.Second
)

// Option defines a function type for configuring Options
type Option func(*Options)

// Options holds the timeout parameters for the client
type Options struct {
	Timeout time.Duration
}

func defaultOptions(opts ...Option) *Options {
	return &Options{
		Timeout: defaultTimeout,
	}
}

// WithTimeout create an option function to set timeout duration
func WithTimeout(timeout time.Duration) Option {
	return func(o *Options) {
		o.Timeout = timeout
	}
}
