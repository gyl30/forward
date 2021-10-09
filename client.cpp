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

#include <random>
#include <chrono>

#include "log.h"
#include "protocol.h"
using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;

std::string random_string(uint32_t length)
{
    static const std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

    std::string tmp;
    tmp.reserve(length);
    while (tmp.size() < length)
    {
        tmp += str;
    }
    tmp.substr(0, length);

    std::random_device rd;
    std::mt19937 generator(rd());

    std::shuffle(tmp.begin(), tmp.end(), generator);
    return tmp;
}

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
struct Rate
{
   public:
    Rate(const std::string& id) : id(id) {}

   public:
    void start() { begin = std::chrono::high_resolution_clock::now(); }
    void finished() { end = std::chrono::high_resolution_clock::now(); }
    void update_read(uint32_t bytes) { read_bytes += bytes; }
    void update_write(uint32_t bytes) { write_bytes += bytes; }

    void dump()
    {
        std::chrono::duration<std::size_t, std::nano> dur = end - begin;
        uint64_t time_count = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        LOG_DEBUG << id << " write bytes " << write_bytes << " spend time " << time_count << " speed "
                  << (write_bytes / time_count);
        LOG_DEBUG << id << " read bytes " << read_bytes << " spend time " << time_count << " speed "
                  << (read_bytes / time_count);
    }

   private:
    std::string id;
    uint64_t write_bytes = 0;
    uint64_t read_bytes = 0;
    std::chrono::high_resolution_clock::time_point begin;
    std::chrono::high_resolution_clock::time_point end;
};

awaitable<void> do_run(tcp::socket socket, std::string address)
{
    asio::error_code ec;
    auto err = redirect_error(use_awaitable, ec);

    Rate r(address);
    r.start();
    while (true)
    {
        {
            std::string msg = random_string(1024);
            std::string packet = protocol::encode_header(msg.size()) + msg;
            co_await socket.async_send(asio::buffer(packet), err);
            if (ec)
            {
                LOG_ERROR << "send failed " << ec.message();
                co_return;
            }
            r.update_write(packet.size());
        }
        {
            char header[4] = {0};
            size_t n = co_await socket.async_read_some(asio::buffer(header, sizeof header), err);
            if (ec)
            {
                LOG_ERROR << "receive failed " << ec.message();
                co_return;
            }
            assert(n == 4);
            uint32_t msg_size = protocol::decode_header(header);
            std::string msg(msg_size, '\0');
            n = co_await socket.async_read_some(asio::buffer(msg.data(), msg_size), err);
            if (ec)
            {
                LOG_ERROR << "receive failed " << ec.message();
                co_return;
            }

            assert(n == msg_size);
            r.update_read(4 + msg_size);

            LOG_DEBUG << "receive finished " << msg;
        }
    }
    r.finished();
    r.dump();
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
    std::string address = socket_address(socket);
    LOG_DEBUG << "connect --> " << address;

    co_await do_run(std::move(socket), std::move(address));
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
