#include <boost/asio.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include "protocol.h"

class connection : public std::enable_shared_from_this<connection>
{
   private:
    using MessageCb = std::function<void(const MsgPkg::codec::SharedVector&)>;
    using ErrorCb = std::function<void(void)>;

   public:
    connection(boost::asio::ip::tcp::socket socket, const std::string& addr) : socket_(std::move(socket)), addr_(addr)
    {
    }

    void start() { do_read_header(MsgPkg::kHeadSize); }

   private:
    void do_read_header(uint32_t head_size)
    {
        auto cb = [self = shared_from_this(), this](const auto& buff)
        {
            uint32_t body_size = MsgPkg::networkToHost32(MsgPkg::peek_uint32_t(buff->data()));
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
            do_write(buff);
            do_read_header(MsgPkg::kHeadSize);
        };
        auto er = [self = shared_from_this(), this]() { close(); };
        do_read_size(body_size, cb, er);
    }

    void do_read_size(uint32_t size, const MessageCb& cb, const ErrorCb& er)
    {
        int minimum_read = size;
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
        boost::asio::async_read(
            socket_, boost::asio::buffer(*buffer), completion_handler,
            [this, buffer, self = shared_from_this(), cb, er](const boost::system::error_code& ec, std::size_t)
            {
                if (ec)
                {
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
            });
    }
    void close()
    {
        socket_.close();
        printf("close address %s", addr_.data());
    }
    void dump_read_vector(const auto& msg)
    {
        std::string buff(msg->begin(), msg->end());
        printf("local <-- %s %s\n", addr_.data(), buff.data());
    }
    void dump_write_vector(const auto& msg)
    {
        std::string buff = MsgPkg::codec::make_decode_shard_vector(msg);
        printf("local --> %s %s\n", addr_.data(), buff.data());
    }
    void do_write(const auto& msg)
    {
        auto buffer = MsgPkg::codec::make_encode_shard_vector(msg);
        boost::asio::async_write(socket_, boost::asio::buffer(*buffer),
                                 [this, buffer, self = shared_from_this()](std::error_code ec, std::size_t)
                                 {
                                     if (ec)
                                     {
                                         close();
                                         return;
                                     }
                                     else
                                     {
                                         dump_write_vector(buffer);
                                     }
                                 });
    }
    std::string addr_;
    boost::asio::ip::tcp::socket socket_;
};

class server
{
   public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
        do_accept();
    }

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
        return address;
    }

    void do_accept()
    {
        acceptor_.async_accept(
            [this](std::error_code ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    auto address = socket_address(socket);
                    if (address.empty())
                    {
                        socket.close();
                    }
                    else
                    {
                        std::make_shared<connection>(std::move(socket), address)->start();
                    }
                }

                do_accept();
            });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
};
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        server s(io_context, std::atoi(argv[1]));

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
