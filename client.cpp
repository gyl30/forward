#include "connection.h"
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

class client : public std::enable_shared_from_this<client>
{
   public:
    client(boost::asio::ip::tcp::socket socket, const std::string& addr)
        : conn(std::make_shared<connection>(std::move(socket), addr))
    {
        printf("client\n");
    };
    ~client() { printf("~client \n"); };

   public:
    void startup()
    {
        conn->set_on_message_cb([this, self = shared_from_this()](const auto& msg) { on_message(msg); });
        conn->set_on_close_cb([this, self = shared_from_this()]() { on_close(); });
        conn->startup();
        conn->write("hello");
    }
    void shutdown() {}

   private:
    void on_message(const MsgPkg::codec::SharedVector& msg)
    {
        std::string m(msg->begin(), msg->end());
        printf("%s\n", m.data());
    }
    void on_close()
    {
        printf("client close\n");
    }

   private:
    std::shared_ptr<connection> conn;
};
int main(int argc, char* argv[])
{
    try
    {
        boost::asio::io_service io_service;
        boost::asio::ip::tcp::socket s(io_service);
        boost::asio::ip::tcp::endpoint end_point(boost::asio::ip::address::from_string("127.0.0.1"), 3200);
        boost::system::error_code ec;
        s.connect(end_point, ec);
        if (ec)
        {
            return -1;
        }
        std::string address = socket_address(s);
        if (address.empty())
        {
            return -1;
        }
        auto cli = std::make_shared<client>(std::move(s), address);
        cli->startup();
        io_service.run();
        printf("hello world\n");
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
