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

#include "connection.h"
#include "common/constants.h"
#include "logger.h"

#include <arpa/inet.h>
#include <securec.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace virt::ovs::ipc::server {

Connection::Connection(int fd, PeerIdentity identity) : fd_(fd), identity_(identity)
{
    LOG_INFO << "Connection created, fd=" << fd_;
}

Connection::Connection(int fd) : fd_(fd)
{
    LOG_INFO << "Connection created, fd=" << fd_;
}

bool Connection::HandleRead()
{
    running_ = true;
    while (running_) {
        bool progressed = false;
        if (state_ == State::READ_LEN) {
            if (!HandleReadLen()) {
                return false;
            }
            progressed = true;
        }
        if (state_ == State::READ_BODY) {
            bool blocked = false;
            if (!HandleReadBody(blocked)) {
                return false;
            }
            if (blocked) {
                return true;
            }
            progressed = true;
        }
        if (!progressed) {
            LOG_DEBUG << "fd= " << fd_ << " READ_LEN ready";
            running_ = false;
            return true;
        }
        if (state_ == State::READY) {
            running_ = false;
            return true;
        }
    }
    return true;
}

bool Connection::HandleReadLen()
{
    static constexpr size_t kLenSize = sizeof(uint32_t);
    if (readBuf_.empty()) {
        readBuf_.resize(kLenSize, '\0');
        lenRead_ = 0;
    }
    ssize_t n = read(fd_, readBuf_.data() + lenRead_, kLenSize - lenRead_);
    if (n > 0) {
        lenRead_ += n;
        if (lenRead_ < kLenSize) {
            LOG_DEBUG << "fd= " << fd_ << " READ_LEN partial, lenRead_=" << lenRead_;
            return true;
        }
        uint32_t lenBE = 0;
        errno_t rc = memcpy_s(&lenBE, sizeof(lenBE), readBuf_.data(), sizeof(lenBE));
        if (rc != EOK) {
            LOG_WARN << "fd=" << fd_ << " READ_LEN error, rc=" << rc;
            return false;
        }

        expectLen_ = ntohl(lenBE);
        readBuf_.clear();
        state_ = State::READ_BODY;
        LOG_DEBUG << "fd= " << fd_ << " READ_LEN done, expectLen_=" << expectLen_;
        return true;
    }
    if (n == 0) {
        LOG_INFO << "fd= " << fd_ << "READ_LEN peer closed";
        return false;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        LOG_DEBUG << "fd= " << fd_ << " READ_LEN EAGAIN or EWOULDBLOCK, lenRead_= " << lenRead_;
        return true;
    }
    LOG_WARN << "fd= " << fd_ << " READ_LEN error, errno=" << errno << " errMsg=" << strerror(errno);
    return false;
}

bool Connection::HandleReadBody(bool &blocked)
{
    blocked = false;
    char buf[constants::MAX_BODY_BUFFER_SIZE];
    ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) {
        readBuf_.append(buf, n);
        LOG_DEBUG << "fd=" << fd_ << " READ_BODY read n=" << n << " total=" << readBuf_.size() << "/" << expectLen_;
        if (readBuf_.size() >= expectLen_) {
            state_ = State::READY;
            LOG_DEBUG << "fd=" << fd_ << " READ_BODY done, READY";
        }
        return true;
    }
    if (n == 0) {
        LOG_INFO << "fd= " << fd_ << "READ_BODY peer closed, current=" << readBuf_.size() << "/" << expectLen_;
        return false;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        LOG_DEBUG << "fd= " << fd_ << " READ_BODY EAGAIN or EWOULDBLOCK, current= " << readBuf_.size() << "/"
                  << expectLen_;
        blocked = true;
        return true;
    }
    LOG_WARN << "fd= " << fd_ << " READ_BODY error, errno=" << errno << " errMsg=" << strerror(errno);
    return false;
}

bool Connection::HandleWrite()
{
    if (writeBuf_.empty()) {
        return true;
    }

    ssize_t n = write(fd_, writeBuf_.data(), writeBuf_.size());
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
    writeBuf_.erase(0, n);

    if (writeBuf_.empty()) {
        state_ = State::READ_LEN;
    }
    return true;
}

bool Connection::NeedWrite() const
{
    return !writeBuf_.empty();
}

bool Connection::HasRequest() const
{
    return state_ == State::READY;
}

std::string Connection::TakeRequest()
{
    state_ = State::PROCESSING;
    LOG_DEBUG << "fd=" << fd_ << " TakeRequest, size=" << readBuf_.size();
    return std::move(readBuf_);
}

void Connection::SetResponse(std::string resp, int epollFd)
{
    uint32_t lenBE = htonl(static_cast<uint32_t>(resp.size()));

    writeBuf_.resize(sizeof(lenBE));
    std::memcpy(writeBuf_.data(), &lenBE, sizeof(lenBE));
    writeBuf_.append(resp);
    state_ = State::WRITE_RESP;

    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = fd_;
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd_, &ev) < 0) {
        LOG_ERROR << "epoll_ctl MOD failed in SetResponse, fd=" << fd_ << ", errno=" << errno
                  << ", errmsg=" << strerror(errno);
    }
    LOG_DEBUG << "fd=" << fd_ << " SetResponse, respSize=" << resp.size();
}

} // namespace virt::ovs::ipc::server