#include "connection.h"

class server_connection : public std::enable_shared_from_this<server_connection>
{
   public:
    server_connection(boost::asio::ip::tcp::socket socket, const std::string& addr)
        : conn(std::make_shared<connection>(std::move(socket), addr)){};
    ~server_connection(){};

   public:
    void startup()
    {
        conn->set_on_message_cb([this, self = shared_from_this()](const auto& msg) { on_message(msg); });
        conn->set_on_close_cb([this, self = shared_from_this()]() { on_close(); });
        conn->startup();
    }
    void shutdown() {}
    void on_message(const MsgPkg::codec::SharedVector& msg)
    {
        conn->write(msg);
    }
    void on_close() {
        printf("server close\n");
    }

   private:
    std::shared_ptr<connection> conn;
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
                        std::make_shared<server_connection>(std::move(socket), address)->startup();
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
        boost::asio::io_context io_context;

        server s(io_context, 3200);

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
