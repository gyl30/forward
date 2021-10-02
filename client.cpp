#include "connection.h"
#include "log.h"
#include <chrono>
#include <thread>

static std::string msg;

class client : public std::enable_shared_from_this<client>
{
   public:
    client(const std::string& ip, uint16_t port) : ip(ip), port(port) { LOG_DEBUG << "client"; };
    ~client() { LOG_DEBUG << "~client"; };

   public:
    void connect()
    {
        do_connect();
        do_run();
    }

   private:
    void do_run()
    {
        LOG_DEBUG << "main loop start";
        boost::system::error_code ec;
        io_service.run(ec);
        if (ec)
        {
            LOG_ERROR << "main loop failed " << ec.message();
        }
        LOG_DEBUG << "main loop finish";
    }

    void do_connect()
    {
        LOG_DEBUG << "do connect";
        auto fn = [this, self(shared_from_this())](const boost::system::error_code& ec)
        {
            if (ec)
            {
                LOG_ERROR << "do connect failed " << ec.message();
                run_timer();
            }
            else
            {
                connection_start();
            }
        };
        s = std::make_unique<boost::asio::ip::tcp::socket>(io_service);
        boost::asio::ip::tcp::endpoint ed(boost::asio::ip::address::from_string(ip), 3200);
        s->async_connect(ed, fn);
    }
    void connection_start()
    {
        conn = std::make_shared<connection>(std::move(*s.release()));
        conn->set_on_message_cb([this, self = shared_from_this()](const auto& msg) { on_message(msg); });
        conn->set_on_close_cb([this, self = shared_from_this()]() { on_close(); });
        conn->startup();
        handshake();
    }
    void handshake() { conn->write("12345$"); }

    void run_keep_timer()
    {
        if (!keep_timer)
        {
            keep_timer = std::make_unique<boost::asio::steady_timer>(io_service);
        }
        keep_timer->expires_from_now(std::chrono::seconds(1));
        keep_timer->async_wait(
            [this, self(shared_from_this())](boost::system::error_code ec)
            {
                if (ec)
                {
                    LOG_ERROR << "keep timer failed " << ec.message();
                    return;
                }
                conn->write(msg);
                run_keep_timer();
            });
    }
    void run_timer()
    {
        if (!timer)
        {
            timer = std::make_unique<boost::asio::steady_timer>(io_service);
        }
        LOG_DEBUG << "run timer";
        timer->expires_from_now(std::chrono::seconds(3));
        timer->async_wait(
            [this, self(shared_from_this())](boost::system::error_code ec)
            {
                if (ec)
                {
                    LOG_ERROR << "timer failed " << ec.message();
                    return;
                }
                do_connect();
            });
    }

   private:
    void on_message(const std::string& msg)
    {
        if (id.empty())
        {
            static const char kDelimiter = '$';
            auto pos = msg.find(kDelimiter);
            if (pos == std::string::npos)
            {
                return;
            }
            id = msg.substr(0, pos);
            run_keep_timer();
        }
        else
        {
            LOG_DEBUG << "read " << msg;
        }
    }
    void on_close()
    {
        LOG_WAR << "client close";
        close_timer();
        do_connect();
    }
    void close_timer()
    {
        boost::system::error_code ec;
        if (timer)
        {
            timer->cancel(ec);
        }
        if (keep_timer)
        {
            keep_timer->cancel(ec);
        }
    }

   private:
    std::string ip;
    uint16_t port;
    std::string id;
    boost::asio::io_service io_service;
    std::unique_ptr<boost::asio::ip::tcp::socket> s;
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::unique_ptr<boost::asio::steady_timer> keep_timer;
    std::shared_ptr<connection> conn;
};

int main(int argc, char* argv[])
{
    assert(argc == 2);
    msg = argv[1];
    LOG_INFO << "start";
    std::make_shared<client>("127.0.0.1", 3200)->connect();
    LOG_INFO << "quit";
    return 0;
}
