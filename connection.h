#ifndef __CONNECTION_H__
#define __CONNECTION_H__
#include <boost/asio.hpp>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>
#include "protocol.h"
#include "log.h"
class connection : public std::enable_shared_from_this<connection>
{
   public:
    using MessageCb = std::function<void(const MsgPkg::codec::SharedVector&)>;
    using ErrorCb = std::function<void(void)>;

   private:
    static std::string socket_address(const boost::asio::ip::tcp::socket& socket)
    {
        boost::system::error_code ec;
        auto ed = socket.remote_endpoint(ec);
        if (ec)
        {
            return "";
        }
        std::string address = ed.address().to_string(ec);
        if (ec)
        {
            return "";
        }
        uint16_t port = ed.port();

        return address + std::to_string(port);
    }

   public:
    connection(boost::asio::ip::tcp::socket socket) : socket_(std::move(socket)), s(socket_.get_executor())
    {
        address_ = socket_address(socket_);
    }
    void set_on_message_cb(const MessageCb& cb) { cb_ = cb; }
    void set_on_close_cb(const ErrorCb& cb) { er_ = cb; }
    void startup() { do_read_header(MsgPkg::kHeadSize); }

    void write(const std::string& msg) { do_write(msg); }
    void write(const MsgPkg::codec::SharedVector& msg) { do_write(msg); }
    std::string address() const { return address_; }

   private:
    void do_read_header(uint32_t head_size)
    {
        auto cb = [self = shared_from_this(), this](const auto& buff)
        {
            uint32_t body_size = MsgPkg::networkToHost32(MsgPkg::peek_uint32_t(buff->data()));
            LOG_DEBUG << "read header finish body size " << body_size;
            do_read_body(body_size);
        };
        auto er = [self = shared_from_this(), this]() { close(); };
        do_read_size(head_size, cb, er);
    }
    void do_read_body(uint32_t body_size)
    {
        auto cb = [self = shared_from_this(), this](const auto& buff)
        {
            dump_read_vector(buff);
            if (cb_)
            {
                cb_(buff);
            }
            do_read_header(MsgPkg::kHeadSize);
        };
        auto er = [self = shared_from_this(), this]() { close(); };
        do_read_size(body_size, cb, er);
    }

    void do_read_size(uint32_t size, const MessageCb& cb, const ErrorCb& er)
    {
        uint32_t minimum_read = size;
        auto completion_handler = [minimum_read](boost::system::error_code ec,
                                                 std::size_t bytes_transferred) -> std::size_t
        {
            if (ec || bytes_transferred >= minimum_read)
            {
                return 0;
            }
            else
            {
                return minimum_read - bytes_transferred;
            }
        };
        auto buffer = MsgPkg::codec::make_shard_vector(minimum_read);

        auto fn = [buffer, self = shared_from_this(), cb, er](const boost::system::error_code& ec, std::size_t)
        {
            if (ec)
            {
                LOG_ERROR << "read failed " << ec.message();
                if (er)
                {
                    er();
                }
                return;
            }
            if (cb)
            {
                cb(buffer);
            }
        };
        auto s_fn = boost::asio::bind_executor(s, fn);
        boost::asio::async_read(socket_, boost::asio::buffer(buffer->data(), buffer->size()), completion_handler, s_fn);
    }
    void close()
    {
        if (er_)
        {
            er_();
        }
        socket_.close();
    }
    void dump_read_vector(const MsgPkg::codec::SharedVector& msg)
    {
        std::string buff(msg->begin(), msg->end());
        LOG_DEBUG << "local <-- " << address_ << " " << buff;
    }
    void dump_write_vector(const MsgPkg::codec::SharedVector& msg)
    {
        std::string buff = MsgPkg::codec::make_decode_shard_vector(msg);
        LOG_DEBUG << "local --> " << address_ << " " << buff;
    }
    void do_write(const std::string& msg)
    {
        auto buffer = MsgPkg::codec::make_encode_shard_vector(msg);
        do_write_help(buffer);
    }
    void do_write(const MsgPkg::codec::SharedVector& msg)
    {
        auto buffer = MsgPkg::codec::make_encode_shard_vector(msg);
        do_write_help(buffer);
    }

   private:
    void do_write_help(const MsgPkg::codec::SharedVector& buffer)
    {
        auto fn = [this, buffer, self = shared_from_this()](std::error_code ec, std::size_t)
        {
            if (ec)
            {
                LOG_ERROR << "write failed " << ec.message();
                close();
                return;
            }
            else
            {
                dump_write_vector(buffer);
            }
        };
        auto s_fn = boost::asio::bind_executor(s, fn);
        boost::asio::async_write(socket_, boost::asio::buffer(*buffer), s_fn);
    }

   private:
    std::string address_;
    MessageCb cb_;
    ErrorCb er_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::executor> s;
};

#endif    //__CONNECTION_H__