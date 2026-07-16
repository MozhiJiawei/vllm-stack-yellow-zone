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

#include "test_connection.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace virt::ovs::ipc::server;
namespace ovs::ut {

TEST_F(TestConnection, ConstructorAndInitialState)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    EXPECT_FALSE(conn.HasRequest());
    EXPECT_FALSE(conn.NeedWrite());

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, HandleReadLenComplete)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    uint32_t len = htonl(4);
    write(fds[0], &len, sizeof(len));

    EXPECT_TRUE(conn.HandleRead());
    EXPECT_FALSE(conn.HasRequest());

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, HandleReadBodyComplete)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    uint32_t len = htonl(5);
    write(fds[0], &len, sizeof(len));
    conn.HandleRead();

    write(fds[0], "hello", 5); // 5 is size of hello
    EXPECT_TRUE(conn.HandleRead());
    EXPECT_TRUE(conn.HasRequest());

    std::string req = conn.TakeRequest();
    EXPECT_EQ(req, "hello");

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, HandleWriteAndSetResponse)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    std::string resp = "world";
    conn.SetResponse(resp, -1);
    EXPECT_TRUE(conn.NeedWrite());

    while (conn.NeedWrite()) {
        conn.HandleWrite();
    }

    EXPECT_FALSE(conn.NeedWrite());

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, PartialReadBody)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    uint32_t len = htonl(5);
    write(fds[0], &len, sizeof(len));
    conn.HandleRead();

    write(fds[0], "he", 2); // 2 is size of he
    EXPECT_TRUE(conn.HandleRead());
    EXPECT_FALSE(conn.HasRequest());

    write(fds[0], "llo", 3); // 3 is size of llo
    EXPECT_TRUE(conn.HandleRead());
    EXPECT_TRUE(conn.HasRequest());
    EXPECT_EQ(conn.TakeRequest(), "hello");

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, HandleReadLenSpecialBranches)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[1]);

    close(fds[0]);
    EXPECT_FALSE(conn.HandleReadLen());

    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    conn = Connection(fds[1]);
    EXPECT_TRUE(conn.HandleReadLen());

    close(fds[0]);
    close(fds[1]);
    conn = Connection(-1);
    EXPECT_FALSE(conn.HandleReadLen());
}

TEST_F(TestConnection, HandleWrite_AllBrabchs_SocketPair)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);

    Connection conn(fds[0]);
    conn.writeBuf_.clear();
    EXPECT_TRUE(conn.HandleWrite());

    conn.writeBuf_ = std::string(65536, 'x'); // 65536 lang buffer to write

    EXPECT_TRUE(conn.HandleWrite());

    close(fds[0]);
    conn.writeBuf_ = "data";
    EXPECT_FALSE(conn.HandleWrite());
    close(fds[1]);
}

TEST_F(TestConnection, HandleReadProgressedFalseBranch)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    Connection conn(fds[1]);

    conn.state_ = Connection::State::PROCESSING;

    EXPECT_TRUE(conn.HandleRead());

    EXPECT_EQ(conn.state_, Connection::State::PROCESSING);

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestConnection, HandleReadProgressedFalseBranch_WriteResp)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    Connection conn(fds[1]);

    conn.state_ = Connection::State::WRITE_RESP;

    EXPECT_TRUE(conn.HandleRead());
    EXPECT_EQ(conn.state_, Connection::State::WRITE_RESP);

    close(fds[0]);
    close(fds[1]);
}
} // namespace ovs::ut