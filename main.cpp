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
//----------------------------------------------------------------------

class chat_session : public std::enable_shared_from_this<chat_session>
{
   public:
    chat_session(tcp::socket socket, const std::string& address)
        : socket_(std::make_unique<tcp::socket>(std::move(socket))),
          address_(address),
          timer_(std::make_unique<asio::steady_timer>(socket_->get_executor()))
    {
        timer_->expires_at(std::chrono::steady_clock::time_point::max());
    }

   public:
    void start()
    {
        auto r = [self = shared_from_this()] { return self->reader(); };
        auto w = [self = shared_from_this()] { return self->writer(); };

        co_spawn(socket_->get_executor(), r, detached);
        co_spawn(socket_->get_executor(), w, detached);
    }

    void deliver(const std::string& msg)
    {
        write_msgs_.push_back(msg);
        asio::error_code ec;
        timer_->cancel_one(ec);
        if (ec)
        {
            LOG_ERROR << address_ << " timer cancel once failed "
                      << ec.message();
        }
        // else
        //{
        // LOG_DEBUG << address_ << " timer cancel once";
        //}
    }

   private:
    awaitable<void> reader()
    {
        while (true)
        {
            auto body_size = co_await read_header();
            if (body_size == -1)
            {
                break;
            }
            auto body = co_await read_body(body_size);
            if (body.empty())
            {
                break;
            }
            LOG_DEBUG << "local <-- " << address_ << " " << body;
            deliver(body);
        }
        stop();
    }
    awaitable<void> writer()
    {
        int ret = 0;
        while (socket_->is_open())
        {
            if (write_msgs_.empty())
            {
                ret = co_await do_wait();
            }
            else
            {
                ret = co_await do_writer();
            }
            if (ret != 0)
            {
                break;
            }
        }
        stop();
    }

    void stop()
    {
        asio::error_code ec;
        if (timer_)
        {
            timer_->cancel(ec);
            if (ec)
            {
                LOG_ERROR << address_ << " timer cancel failed "
                          << ec.message();
            }
            else
            {
                LOG_DEBUG << address_ << " timer cancel";
            }
            timer_.reset();
        }
        if (socket_ && socket_->is_open())
        {
            socket_->close(ec);
            if (ec)
            {
                LOG_ERROR << address_ << " close failed " << ec.message();
            }
            else
            {
                LOG_DEBUG << address_ << " close";
            }

            LOG_DEBUG << address_ << " DOWN";
        }
    }

    awaitable<int32_t> read_header()
    {
        static const int kHeaderSize = 4;
        std::vector<char> buffer;
        buffer.reserve(kHeaderSize);
        auto ret = co_await read_size(buffer.data(), kHeaderSize);
        if (ret != 0)
        {
            co_return ret;
        }
        char buf[kHeaderSize + 1] = {0};
        ::strncat(buf, (char*)buffer.data(), kHeaderSize);
        co_return ::atoi(buf);
    }
    awaitable<std::string> read_body(uint32_t body_size)
    {
        std::vector<char> buffer;
        buffer.reserve(body_size);
        auto ret = co_await read_size(buffer.data(), body_size);
        if (ret != 0)
        {
            co_return std::string{};
        }

        std::string msg(buffer.data(), body_size);
        co_return msg;
    }

    awaitable<int> read_size(char* buffer, uint32_t size)
    {
        asio::error_code ec;
        auto err = redirect_error(use_awaitable, ec);

        uint32_t read_bytes = 0;
        while (read_bytes < size)
        {
            char* b = buffer + read_bytes;
            auto bytes = size - read_bytes;
            auto n =
                co_await socket_->async_read_some(asio::buffer(b, bytes), err);
            if (ec)
            {
                co_return -1;
            }
            read_bytes += n;
        }
        co_return 0;
    }

    awaitable<int> do_writer()
    {
        assert(!write_msgs_.empty());
        asio::error_code ec;
        auto err = redirect_error(use_awaitable, ec);

        std::string msg = write_msgs_.front();
        write_msgs_.pop_front();
        co_await asio::async_write(*socket_, asio::buffer(msg), err);
        if (ec)
        {
            LOG_ERROR << address_ << " write failed " << ec.message();
            co_return -1;
        }
        LOG_DEBUG << "local --> " << address_ << " " << msg;
        co_return 0;
    }
    awaitable<int> do_wait()
    {
        asio::error_code ec;
        co_await timer_->async_wait(redirect_error(use_awaitable, ec));
        if (ec && ec != asio::error::operation_aborted)
        {
            LOG_ERROR << address_ << " timer wait failed " << ec.message();
            co_return -1;
        }
        co_return 0;
    }

   private:
    std::unique_ptr<tcp::socket> socket_;
    std::string address_;
    std::unique_ptr<asio::steady_timer> timer_;
    std::deque<std::string> write_msgs_;
};

awaitable<void> listener(tcp::acceptor acceptor)
{
    for (;;)
    {
        asio::error_code ec;
        auto err = redirect_error(use_awaitable, ec);

        auto socket = co_await acceptor.async_accept(err);
        if (ec)
        {
            LOG_ERROR << "acceptor failed " << ec.message();
        }
        else
        {
            std::string address = socket_address(socket);
            LOG_DEBUG << address << " UP";
            std::make_shared<chat_session>(std::move(socket), address)->start();
        }
    }
}

int main(int argc, char* argv[])
{
    asio::io_context io(1);

    unsigned short port = 5555;
    LOG_INFO << argv[0] << " run on " << port;
    co_spawn(io, listener(tcp::acceptor(io, {tcp::v4(), port})), detached);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io.stop(); });

    asio::error_code ec;
    io.run(ec);

    LOG_INFO << argv[0] << " quit";
    return 0;
}
