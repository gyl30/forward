#ifndef __CONNECTION_H__
#define __CONNECTION_H__
#include <boost/asio.hpp>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>
#include <string_view>
#include "protocol.h"
#include "log.h"
class connection : public std::enable_shared_from_this<connection>
{
   private:
    using MessageCb = std::function<void(const MsgPkg::SharedVector&)>;
    using ErrorCb = std::function<void(void)>;

   private:
    static std::string socket_address(const boost::asio::ip::tcp::socket& socket)
    {
        boost::system::error_code ec;
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

   public:
    connection(boost::asio::ip::tcp::socket socket) : socket_(std::move(socket)), s(socket_.get_executor())
    {
        address_ = socket_address(socket_);
        if (address_.empty())
        {
            LOG_ERROR << "socket address failed";
        }
        boost::system::error_code ec;
        socket_.set_option(boost::asio::ip::tcp::no_delay(true), ec);
        if (ec)
        {
            LOG_ERROR << address_ << " set no delay failed " << ec.message();
        }
        boost::asio::socket_base::receive_buffer_size r_option(4096);
        socket_.set_option(r_option, ec);
        if (ec)
        {
            LOG_ERROR << address_ << " set receive buffer size failed " << ec.message();
        }

        boost::asio::socket_base::send_buffer_size w_option(4096);
        socket_.set_option(w_option, ec);
        if (ec)
        {
            LOG_ERROR << address_ << " set send buffer size failed " << ec.message();
        }
    }
    void set_on_message_cb(const std::function<void(const std::string&)>& cb) { msg_cb_ = cb; }
    void set_on_close_cb(const std::function<void(void)>& cb) { close_cb_ = cb; }

    void startup() { do_read_header(MsgPkg::kHeadSize); }
    void shutdown() { do_shutdown(); }

    void set_read_timeout(uint32_t timeout) { this->timeout = timeout; }

    void write(const std::string& msg) { do_write(msg); }
    std::string address() const { return address_; }

   private:
    void dump_read_vector(const std::string& msg) { LOG_DEBUG << "local <-- " << address_ << " " << msg; }
    void dump_write_vector(const std::string& msg) { LOG_DEBUG << "local --> " << address_ << " " << msg; }

    void dump_read_vector(std::string_view s) { LOG_DEBUG << "local <-- " << address_ << " " << s; }
    void dump_write_vector(std::string_view s) { LOG_DEBUG << "local --> " << address_ << " " << s; }

    void dump_read_vector(const MsgPkg::SharedVector& msg)
    {
        std::string buff = MsgPkg::codec::decode(msg);
        LOG_DEBUG << "local <-- " << address_ << " " << buff;
    }
    void dump_write_vector(const MsgPkg::SharedVector& msg)
    {
        std::string buff = MsgPkg::codec::decode(msg);
        LOG_DEBUG << "local --> " << address_ << " " << buff;
    }
    void do_write(const std::string& msg)
    {
        auto buffer = MsgPkg::codec::encode(msg);
        do_write_help(buffer);
    }

    void do_shutdown()
    {
        msg_cb_ = nullptr;
        close_cb_ = nullptr;
        close();
    }
    void close()
    {
        if (close_cb_)
        {
            close_cb_();
            close_cb_ = nullptr;
        }
        boost::system::error_code ec;
        if (timer)
        {
            timer->cancel(ec);
            if (ec)
            {
                LOG_ERROR << address_ << " cancel timer faled " << ec.message();
            }
            else
            {
                LOG_DEBUG << address_ << " cancel timer";
            }
            timer.reset();
            timer = nullptr;
        }
        if (!socket_.is_open())
        {
            LOG_WAR << address_ << " is closed";
            return;
        }
        socket_.cancel(ec);
        if (ec)
        {
            LOG_ERROR << address_ << " socket cancel faled " << ec.message();
        }
        else
        {
            LOG_DEBUG << address_ << " cancel socket";
        }

        socket_.close(ec);
        if (ec)
        {
            LOG_ERROR << address_ << " close socket faled " << ec.message();
        }
        else
        {
            LOG_DEBUG << address_ << " close socket";
        }
    }

   private:
    void do_read_header(uint32_t head_size)
    {
        auto cb = [self = shared_from_this(), this](const auto& buff)
        {
            uint32_t body_size = MsgPkg::codec::body_size(buff);
            //LOG_DEBUG << "read header finish body size " << body_size;
            do_read_body(body_size);
        };
        auto er = [self = shared_from_this(), this]() { close(); };
        do_read_size(head_size, cb, er);
    }
    void do_read_body(uint32_t body_size)
    {
        auto cb = [self = shared_from_this(), this](const auto& buff)
        {
            if (msg_cb_)
            {
                dump_read_vector(std::string_view((char*)buff->data(), buff->size()));
                msg_cb_(std::string(buff->begin(), buff->end()));
            }
            do_read_header(MsgPkg::kHeadSize);
        };
        auto er = [self = shared_from_this(), this]() { close(); };
        do_read_size(body_size, cb, er);
    }

    void do_read_size(uint32_t size, const MessageCb& cb, const ErrorCb& er)
    {
        start_wait_input_timer();
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
        auto buffer = MsgPkg::make_shard_vector(minimum_read);

        auto fn = [buffer, this, self = shared_from_this(), cb, er](const boost::system::error_code& ec, std::size_t)
        {
            if (ec)
            {
                if (ec == boost::asio::error::bad_descriptor)
                {
                    LOG_WAR << address_ << " closed";
                }
                else if (ec == boost::asio::error::connection_reset)
                {
                    LOG_WAR << address_ << " closed";
                }
                else if (ec == boost::asio::error::eof)
                {
                    LOG_WAR << address_ << " closed";
                }
                else
                {
                    LOG_ERROR << address_ << "read failed " << ec.message();
                }
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
    void do_write_help(const MsgPkg::SharedVector& buffer)
    {
        auto fn = [this, buffer, self = shared_from_this()](boost::system::error_code ec, std::size_t)
        {
            if (ec)
            {
                if (ec == boost::asio::error::bad_descriptor)
                {
                    LOG_WAR << address_ << " closed";
                }
                else if (ec == boost::asio::error::connection_reset)
                {
                    LOG_WAR << address_ << " closed";
                }
                else if (ec == boost::asio::error::eof)
                {
                    LOG_WAR << address_ << " closed";
                }
                else
                {
                    LOG_ERROR << "write to " << address_ << " failed " << ec.message();
                }
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
    void start_wait_input_timer()
    {
        if (!timeout)
        {
            return;
        }
        if (!timer)
        {
#if BOOST_VERSION < 107000
            timer = std::make_unique<boost::asio::steady_timer>(socket_.get_io_context());
#else
            timer = std::make_unique<boost::asio::steady_timer>(s);
#endif
        }
        if (timer)
        {
            run_timer();
        }
    }
    void run_timer()
    {
        timer->expires_after(std::chrono::seconds(timeout));
        auto fn = [this, self(shared_from_this())](boost::system::error_code ec)
        {
            if (ec && ec == boost::system::errc::operation_canceled)
            {
                // LOG_DEBUG << address_ << " timer cancel";
                return;
            }
            if (ec)
            {
                LOG_ERROR << address_ << " timer failed " << ec.message();
            }
            else
            {
                do_timeout();
            }
        };
        auto s_fn = boost::asio::bind_executor(s, fn);
        timer->async_wait(s_fn);
    }
    void do_timeout()
    {
        if (timer->expiry() <= boost::asio::steady_timer::clock_type::now())
        {
            LOG_ERROR << address_ << " timeout";
            close();
        }
        else
        {
            run_timer();
        }
    }

   private:
    std::function<void(void)> close_cb_;
    std::function<void(const std::string&)> msg_cb_;

    uint32_t timeout = 0;
    std::string address_;
    boost::asio::ip::tcp::socket socket_;
    std::unique_ptr<boost::asio::steady_timer> timer;
    boost::asio::strand<boost::asio::ip::tcp::socket::executor_type> s;
};

#endif    //__CONNECTION_H__
