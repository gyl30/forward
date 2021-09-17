#include "connection.h"
#include "log.h"

class server_connection : public std::enable_shared_from_this<server_connection>
{
   public:
    server_connection(boost::asio::ip::tcp::socket socket) : conn(std::make_shared<connection>(std::move(socket))){};
    ~server_connection(){};

   public:
    void startup()
    {
        conn->set_on_message_cb([this, self = shared_from_this()](const auto& msg) { on_message(msg); });
        conn->set_on_close_cb([this, self = shared_from_this()]() { on_close(); });
        conn->startup();
    }
    void shutdown() {}
    void on_message(const MsgPkg::codec::SharedVector& msg) { conn->write(msg); }
    void on_close() { LOG_DEBUG << "server close"; }

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
    void do_accept()
    {
        acceptor_.async_accept(
            [this](std::error_code ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<server_connection>(std::move(socket))->startup();
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
        LOG_ERROR << "Exception: " << e.what();
    }

    return 0;
}
