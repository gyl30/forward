#include <memory>
#include "connection.h"
#include "log.h"

class server_connection;
using ConnectionPtr = std::shared_ptr<server_connection>;

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
        conn->set_read_timeout(10);
        conn->startup();
    }
    void shutdown() { conn->shutdown(); }
    void set_on_message_cb(const std::function<void(const ConnectionPtr&, const std::string&)>& cb) { msg_cb_ = cb; }
    void set_on_close_cb(const std::function<void(const ConnectionPtr&)>& cb) { close_cb_ = cb; }
    std::string address() const { return conn->address(); }
    void write(const std::string& msg) { conn->write(msg); }

   private:
    void on_message(const std::string& msg)
    {
        if (msg_cb_)
        {
            msg_cb_(shared_from_this(), msg);
        }
    }

    void on_close()
    {
        if (close_cb_)
        {
            close_cb_(shared_from_this());
        }
    }

   private:
    std::function<void(const ConnectionPtr&)> close_cb_;
    std::function<void(const ConnectionPtr&, const std::string&)> msg_cb_;
    std::shared_ptr<connection> conn;
};
class server
{
   public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
    }

   public:
    void startup() { do_accept(); }
    void shutdown() { do_shutdown(); }
    void set_on_message_cb(const std::function<void(const ConnectionPtr&, const std::string&)>& cb) { msg_cb_ = cb; }
    void set_on_close_cb(const std::function<void(const ConnectionPtr&)>& cb) { close_cb_ = cb; }
    void set_on_connected_cb(const std::function<void(const ConnectionPtr&)>& cb) { connected_cb_ = cb; }

   private:
    void do_shutdown()
    {
        boost::system::error_code ec;
        acceptor_.cancel(ec);
        if (ec)
        {
            LOG_ERROR << "acceptor cancel failed " << ec.message();
        }
        else
        {
            LOG_DEBUG << "acceptor cancel";
        }
        acceptor_.close(ec);
        if (ec)
        {
            LOG_ERROR << "acceptor close failed " << ec.message();
        }
        else
        {
            LOG_DEBUG << "acceptor close";
        }
    }
    void do_accept()
    {
        auto fn = [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket)
        {
            if (!ec)
            {
                create_connection(std::move(socket));
            }

            do_accept();
        };
        acceptor_.async_accept(fn);
    }

   private:
    void create_connection(boost::asio::ip::tcp::socket socket)
    {
        auto conn = std::make_shared<server_connection>(std::move(socket));
        conn->set_on_message_cb([this](const ConnectionPtr& conn, const std::string& msg) { on_message(conn, msg); });
        conn->set_on_close_cb([this](const ConnectionPtr& conn) { on_close(conn); });
        conn->startup();
        on_connected(conn);
    }

    void on_connected(const ConnectionPtr& conn)
    {
        if (connected_cb_)
        {
            connected_cb_(conn);
        }
    }
    void on_message(const ConnectionPtr& conn, const std::string& msg)
    {
        if (msg_cb_)
        {
            msg_cb_(conn, msg);
        }
    }

    void on_close(const ConnectionPtr& conn)
    {
        if (close_cb_)
        {
            close_cb_(conn);
        }
        else
        {
            LOG_DEBUG << "server close";
        }
    }

   private:
    std::function<void(const ConnectionPtr&)> close_cb_;
    std::function<void(const ConnectionPtr&)> connected_cb_;
    std::function<void(const ConnectionPtr&, const std::string&)> msg_cb_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

class service
{
   public:
    service() {}
    ~service() {}

   public:
    void startup()
    {
        s.set_on_message_cb([this](const ConnectionPtr& conn, const std::string& msg) { on_message(conn, msg); });
        s.set_on_close_cb([this](const ConnectionPtr& conn) { on_close(conn); });
        s.set_on_connected_cb([this](const ConnectionPtr& conn) { on_connected(conn); });
        s.startup();
    }
    void shutdown()
    {
        s.shutdown();
        io_context.stop();
    }

    void run()
    {
        boost::system::error_code ec;
        io_context.run(ec);
        if (ec)
        {
            LOG_ERROR << "server stop failed " << ec.message();
        }
        else
        {
            LOG_DEBUG << "server stop ";
        }
    }

   private:
    void on_message(const ConnectionPtr& conn, const std::string& msg)
    {
        LOG_DEBUG << conn->address() << " " << msg;
        conn->write(msg);
        //conn->shutdown();
    }

    void on_connected(const ConnectionPtr& conn)
    {
        LOG_DEBUG << conn->address() << " "
                  << "connected";
    }
    void on_close(const ConnectionPtr& conn)
    {
        LOG_WAR << conn->address() << " "
                << "close";
    }

   private:
    boost::asio::io_context io_context;
    server s{io_context, 3200};
};

int main(int argc, char* argv[])
{
    LOG_INFO << "service start";
    service s;
    s.startup();
    s.run();
    LOG_INFO << "service finish";

    return 0;
}
