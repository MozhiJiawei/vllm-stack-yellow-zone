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

#include "test_server.h"
#include "config_module.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>

using namespace virt::ovs::ipc::server;
using namespace virt::ovs;
using namespace virt::ovs::msg;
namespace ovs::ut {

static void SetNonBlock(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

TEST_F(TestServer, AuthorizeService)
{
    EXPECT_TRUE(AuthManager::AuthorizeService("a, b, c", "a"));
    EXPECT_TRUE(AuthManager::AuthorizeService("a, b, c", "b"));
    EXPECT_FALSE(AuthManager::AuthorizeService("a, b, c", "d"));
    EXPECT_FALSE(AuthManager::AuthorizeService("", "a"));
}

TEST_F(TestServer, UidToUsername)
{
    uid_t uid = getuid();
    std::string name = Server::UidToUsername(uid);
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(Server::UidToUsername(999999).empty()); // 999999 is a non-existent UID
}

TEST_F(TestServer, PrepareSocketDir)
{
    std::string path = "/tmp/ubs_test/socket";
    Server s(path, 1);

    EXPECT_TRUE(s.PrepareSocketDir());
}

TEST_F(TestServer, AcceptClient)
{
    std::string path = "/tmp/ubs_test/socket";
    unlink(path.c_str());

    Server s(path, 1);
    ASSERT_TRUE(s.InitListenSocket());
    ASSERT_TRUE(s.InitEpoll());

    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(client, 0);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ASSERT_GT(snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str()), 0);
    ASSERT_EQ(connect(client, static_cast<sockaddr *>(static_cast<void *>(&addr)), sizeof(addr)), 0);

    s.AcceptClients();
    close(client);
}

TEST_F(TestServer, AcceptClient_UserError)
{
    std::string path = "/tmp/ubs_test/socket";
    unlink(path.c_str());
    Server s(path, 1);
    ASSERT_TRUE(s.InitListenSocket());
    ASSERT_TRUE(s.InitEpoll());

    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(client, 0);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ASSERT_GT(snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str()), 0);
    ASSERT_EQ(connect(client, static_cast<sockaddr *>(static_cast<void *>(&addr)), sizeof(addr)), 0);
    MOCKER(Server::UidToUsername).stubs().will(returnValue(std::string("")));
    s.AcceptClients();
    MOCKER(Server::UidToUsername).reset();
    MOCKER(epoll_ctl).stubs().will(returnValue(-1));
    s.AcceptClients();
    close(client);
    MOCKER(epoll_ctl).reset();
}

TEST_F(TestServer, HandleWriteEvent)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    SetNonBlock(fds[0]);
    SetNonBlock(fds[1]);

    PeerIdentity id{};
    id.uid = getuid();
    id.username = Server::UidToUsername(id.uid);

    Connection conn(fds[0], id);

    std::string resp = "hello";
    conn.SetResponse(resp, -1);

    Server s("/tmp/ubs_test/socket", 1);

    ASSERT_TRUE(s.InitListenSocket());
    ASSERT_TRUE(s.InitEpoll());

    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = fds[0];
    ASSERT_EQ(epoll_ctl(s.epollFd_, EPOLL_CTL_ADD, fds[0], &ev), 0);

    ASSERT_TRUE(s.HandleWriteEvent(conn, fds[0]));
    EXPECT_FALSE(conn.NeedWrite());

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestServer, CloseConnection)
{
    std::string path = "/tmp/ubs_test/socket";
    Server s(path, 1);

    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    PeerIdentity id{};
    id.uid = getuid();
    id.username = Server::UidToUsername(id.uid);

    auto conn = std::make_shared<Connection>(fds[0], id);
    s.conns_[fds[0]] = conn;
    s.CloseConnection(fds[0]);

    close(fds[1]);
}

TEST_F(TestServer, StartAndStop)
{
    Server server("/tmp/ubs_test/socket", 1);
    server.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // sleep 50 ms
    server.Stop();
    SUCCEED();
}

TEST_F(TestServer, SocketFail)
{
    Server s("/tmp/ubs_test/socket", 1);
    MOCKER(socket).stubs().will(returnValue(-1));
    EXPECT_FALSE(s.InitListenSocket());
    MOCKER(socket).reset();
}

TEST_F(TestServer, BindListenFail)
{
    Server s("/tmp/ubs_test/socket", 1);
    MOCKER(bind).stubs().will(returnValue(-1));
    EXPECT_FALSE(s.InitListenSocket());
    MOCKER(bind).reset();
}

TEST_F(TestServer, EpollCreateFail)
{
    Server s("/tmp/ubs_test/socket", 1);
    MOCKER(epoll_create1).stubs().will(returnValue(-1));
    EXPECT_FALSE(s.InitEpoll());
    MOCKER(epoll_create1).reset();
}

TEST_F(TestServer, EpollCtlAddFail)
{
    Server s("/tmp/ubs_test/socket", 1);
    MOCKER(epoll_create1).stubs().will(returnValue(-1));
    MOCKER(epoll_ctl).stubs().will(returnValue(-1));
    EXPECT_FALSE(s.InitEpoll());
    MOCKER(epoll_create1).reset();
    MOCKER(epoll_ctl).reset();
}

TEST_F(TestServer, AcceptFailErrnoOtherThanEAGAIN)
{
    Server s("/tmp/ubs_test/socket", 1);

    MOCKER(accept).stubs().will(returnValue(-1));
    errno = EBADF;
    s.AcceptClients();

    MOCKER(accept).reset();
}

TEST_F(TestServer, AcceptEagain)
{
    Server s("/tmp/ubs_test/socket", 1);
    MOCKER(accept).stubs().will(returnValue(-1));
    errno = EAGAIN;
    s.AcceptClients();

    MOCKER(accept).reset();
}

TEST_F(TestServer, GetsockoptFail)
{
    Server s("/tmp/ubs_test/socket", 1);

    MOCKER(accept).stubs().will(returnValue(100)).then(returnValue(-1)); // mock accept return 100
    errno = EAGAIN;

    MOCKER(getsockopt).stubs().will(returnValue(-1));
    s.AcceptClients();
    MOCKER(getsockopt).reset();
    MOCKER(accept).reset();
}

std::string MakeTestRequest()
{
    IpcRequest req;
    req.service_ = "svc";
    req.method_ = "meth";
    virt::ovs::VirtMsgPacker packer;
    req.Serialize(packer);
    return packer.String();
}

TEST_F(TestServer, HandleBusiness_AuthorizeUserFail)
{
    Server server("/tmp/ubs_test/socket");
    auto conn = std::make_shared<Connection>(42);

    auto rewReq = MakeTestRequest();
    MOCKER(AuthManager::AuthorizeUser).stubs().will(returnValue(false));

    EXPECT_NO_THROW(server.HandleBusiness(conn, rewReq));

    MOCKER(AuthManager::AuthorizeUser).reset();
}

TEST_F(TestServer, HandleBusiness_AuthorizeServiceFail)
{
    Server server("/tmp/ubs_test/socket");
    auto conn = std::make_shared<Connection>(42);

    auto rewReq = MakeTestRequest();
    MOCKER(AuthManager::AuthorizeUser).stubs().will(returnValue(true));
    MOCKER(AuthManager::AuthorizeService).stubs().will(returnValue(false));

    EXPECT_NO_THROW(server.HandleBusiness(conn, rewReq));

    MOCKER(AuthManager::AuthorizeUser).reset();
    MOCKER(AuthManager::AuthorizeService).reset();
}

TEST_F(TestServer, HandleBusiness_DispatchOk)
{
    Server server("/tmp/ubs_test/socket");
    auto conn = std::make_shared<Connection>(42);

    auto rewReq = MakeTestRequest();
    MOCKER(AuthManager::AuthorizeUser).stubs().will(returnValue(true));
    MOCKER(AuthManager::AuthorizeService).stubs().will(returnValue(true));

    EXPECT_NO_THROW(server.HandleBusiness(conn, rewReq));

    MOCKER(AuthManager::AuthorizeUser).reset();
    MOCKER(AuthManager::AuthorizeService).reset();
}

class MockerServer : public Server {
public:
    using Server::Server;
    bool throwInDispatch = false;
    IpcResponse Dispatch(const IpcRequest &req)
    {
        if (throwInDispatch) {
            throw std::runtime_error("dispatch error");
        }
        return IpcResponse(static_cast<uint32_t>(VirtIPCCode::OK));
    }
};

TEST_F(TestServer, HandleBusiness_DispatchExecption)
{
    MockerServer server("/tmp/unused.sock");
    auto conn = std::make_shared<Connection>(42);
    auto rewReq = MakeTestRequest();
    MOCKER(AuthManager::AuthorizeUser).stubs().will(returnValue(true));
    MOCKER(AuthManager::AuthorizeService).stubs().will(returnValue(true));

    server.throwInDispatch = true;

    EXPECT_NO_THROW(server.HandleBusiness(conn, rewReq));

    MOCKER(AuthManager::AuthorizeUser).reset();
    MOCKER(AuthManager::AuthorizeService).reset();
}

TEST_F(TestServer, PrepareSocketDir_DirAlreadyExists)
{
    Server server("/tmp/ubs_test/socket");

    MOCKER(std::filesystem::exists).stubs().will(returnValue(true));

    EXPECT_TRUE(server.PrepareSocketDir());

    MOCKER(std::filesystem::exists).reset();
}

TEST_F(TestServer, PrepareSocketDir_CreateDirSuccess)
{
    Server server("/tmp/ubs_test/socket");

    MOCKER(std::filesystem::exists).stubs().will(returnValue(false));
    MOCKER(std::filesystem::create_directory).stubs().will(returnValue(true));

    EXPECT_TRUE(server.PrepareSocketDir());

    MOCKER(std::filesystem::create_directory).reset();
    MOCKER(std::filesystem::exists).reset();
}

TEST_F(TestServer, PrepareSocketDir_CreateDirThrows)
{
    Server server("/proc/ubsvirt/ovs.sock");

    EXPECT_FALSE(server.PrepareSocketDir());
}

TEST_F(TestServer, HandleReadEvent_QpsLimitExceeded)
{
    Server server("/tmp/ubs_test/socket");
    auto conn = std::make_shared<Connection>(10); // mock fd is 10

    server.qpsLimit_ = 0;

    EXPECT_TRUE(server.HandleReadEvent(conn, 10)); // mock fd is 10
}

TEST_F(TestServer, HandleRead_AllBranches)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn(fds[0]);

    close(fds[0]);
    conn.state_ = Connection::State::READ_LEN;
    EXPECT_FALSE(conn.HandleRead());

    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
    Connection conn2(fds[1]);

    conn2.state_ = Connection::State::READ_LEN;
    uint32_t len = htonl(8);
    write(fds[0], &len, 4); // 4 is len of char count
    EXPECT_TRUE(conn2.HandleRead());

    close(fds[0]);
    close(fds[1]);
}

class MockConfigModule : public config::ConfigModule {
public:
    config::ConfigCode ret{config::ConfigCode::OK};
    std::string authorityValue;

    config::ConfigCode GetConf(const std::string &section, const std::string &username, std::string &authority)
    {
        if (ret != config::ConfigCode::OK) {
            return ret;
        }
        authority = authorityValue;
        return config::ConfigCode::OK;
    }
};

TEST_F(TestServer, AuthorizeUser_GetConfFailed)
{
    MockConfigModule conf;
    conf.ret = config::ConfigCode::CONFIG_FILE_READ_ERROR;

    std::string authority;
    EXPECT_FALSE(AuthManager::AuthorizeUser("testuser", authority, conf));
}

static config::ConfigCode FakeGetConfString(config::ConfigModule *, const std::string &section, const std::string &key,
                                            std::string &val)
{
    val = "svc1,svc2";
    return config::ConfigCode::OK;
}

TEST_F(TestServer, AuthorizeUser_GetConfSuccess)
{
    std::string authority;

    MOCKER((config::ConfigCode(config::ConfigModule::*)(const std::string &, const std::string &, std::string &)) &
           config::ConfigModule::GetConf<std::string>)
        .stubs()
        .will(invoke(FakeGetConfString));

    EXPECT_TRUE(AuthManager::AuthorizeUser("testuser", authority, config::ConfigModule::GetInstance()));

    EXPECT_EQ(authority, "svc1,svc2");

    GlobalMockObject::verify();
}

TEST_F(TestServer, HandleReadEvent_HandleReadReturnsFalse)
{
    Server s("/tmp/ubs_test/socket");
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);

    PeerIdentity id{};
    id.uid = getuid();
    id.username = Server::UidToUsername(id.uid);
    auto conn = std::make_shared<Connection>(fds[0], id);

    s.qpsLimit_ = 1000;
    close(fds[0]);
    close(fds[1]);

    EXPECT_FALSE(s.HandleReadEvent(conn, fds[0]));
}

TEST_F(TestServer, HandleReadEvent_PoolFull)
{
    Server s("/tmp/ubs_test/socket");

    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);

    PeerIdentity id{};
    id.uid = getuid();
    id.username = Server::UidToUsername(id.uid);
    auto conn = std::make_shared<Connection>(fds[0], id);

    s.qpsLimit_ = 1000;

    std::string body = "test";
    uint32_t len = htonl(static_cast<uint32_t>(body.size()));
    write(fds[1], &len, sizeof(len));
    write(fds[1], body.data(), body.size());

    MOCKER(&ThreadPool::TryEnqueue).stubs().will(returnValue(false));
    EXPECT_FALSE(s.HandleReadEvent(conn, fds[0]));
    MOCKER(&ThreadPool::TryEnqueue).reset();

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestServer, HandleWriteEvent_EpollCtlModFail)
{
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    SetNonBlock(fds[0]);
    SetNonBlock(fds[1]);

    PeerIdentity id{};
    id.uid = getuid();
    id.username = Server::UidToUsername(id.uid);

    Connection conn(fds[0], id);
    conn.writeBuf_ = "data";

    Server s("/tmp/ubs_test/socket", 1);
    ASSERT_TRUE(s.InitListenSocket());
    ASSERT_TRUE(s.InitEpoll());

    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = fds[0];
    ASSERT_EQ(epoll_ctl(s.epollFd_, EPOLL_CTL_ADD, fds[0], &ev), 0);

    MOCKER(epoll_ctl).stubs().will(returnValue(-1));
    EXPECT_FALSE(s.HandleWriteEvent(conn, fds[0]));
    MOCKER(epoll_ctl).reset();

    close(fds[0]);
    close(fds[1]);
}

TEST_F(TestServer, InitListenSocket_PrepareSocketDirFail)
{
    Server s("/proc/nonexistent_subdir_xyz/socket", 1);
    EXPECT_FALSE(s.InitListenSocket());
}

} // namespace ovs::ut