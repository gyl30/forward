#include "connection.h"
#include "log.h"
class client : public std::enable_shared_from_this<client>
{
   public:
    client(boost::asio::ip::tcp::socket socket) : conn(std::make_shared<connection>(std::move(socket)))
    {
        LOG_DEBUG << "client " << conn->address();
    };
    ~client() { LOG_DEBUG << "~client " << conn->address(); };

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
        LOG_DEBUG << "read " << m;
        conn->write(m);
    }
    void on_close() { LOG_WAR << "client close"; }

   private:
    std::shared_ptr<connection> conn;
};
int main(int argc, char* argv[])
{
    try
    {
        LOG_INFO << "start";
        boost::asio::io_service io_service;
        boost::asio::ip::tcp::socket s(io_service);
        boost::asio::ip::tcp::endpoint end_point(boost::asio::ip::address::from_string("127.0.0.1"), 3200);
        boost::system::error_code ec;
        s.connect(end_point, ec);
        if (ec)
        {
            return -1;
        }

        std::make_shared<client>(std::move(s))->startup();

        io_service.run();
        LOG_INFO << "quit";
    }
    catch (std::exception& e)
    {
        LOG_ERROR << "Exception: " << e.what();
    }

    return 0;
}
