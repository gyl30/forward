#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <asio/awaitable.hpp>
#include <asio/detached.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include "log.h"

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;

static std::string socket_address(const asio::ip::tcp::socket& socket)
{
    asio::error_code ec;
    auto ed = socket.remote_endpoint(ec);
    if (ec)
    {
        LOG_ERROR << "socket remote endpoint failed " << ec.message();
        return "";
    }
    std::string address = ed.address().to_string(ec);
    if (ec)
    {
        LOG_ERROR << "socket remote address to string failed " << ec.message();
        return "";
    }
    uint16_t port = ed.port();

    return address + ":" + std::to_string(port);
}

awaitable<void> client(tcp::socket socket, asio::ip::tcp::endpoint ed)
{
    asio::error_code ec;
    auto err = redirect_error(use_awaitable, ec);
    co_await socket.async_connect(ed, err);
    if (ec)
    {
        LOG_ERROR << "connect failed " << ec.message();
        co_return;
    }
    LOG_DEBUG << "connect --> " << socket_address(socket);
    char buff[256] = "0005hello";
    co_await socket.async_send(asio::buffer(buff, sizeof(buff)), err);
    if (ec)
    {
        LOG_ERROR << "send failed " << ec.message();
        co_return;
    }
    auto n = co_await socket.async_receive(asio::buffer(buff), err);
    if (ec)
    {
        LOG_ERROR << "receive failed " << ec.message();
        co_return;
    }
    LOG_DEBUG << "receive finished " << n << " bytes";
}

int main(int argc, char* argv[])
{
    asio::io_context io(1);
    std::string address = argv[1];
    uint16_t port = ::atoi(argv[2]);
    LOG_INFO << argv[0] << " run on " << address << ":" << port;

    tcp::socket s(io);
    for (int i = 0; i < 100; i++)
    {
        tcp::endpoint ed{asio::ip::address::from_string(address), port};
        co_spawn(io, client(std::move(s), std::move(ed)), detached);
    }

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io.stop(); });

    asio::error_code ec;
    io.run(ec);

    LOG_INFO << argv[0] << " quit";
    return 0;
}
