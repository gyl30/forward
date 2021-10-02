#include <memory>
#include "connection.h"
#include "log.h"
#include "boost/asio/signal_set.hpp"
#include "boost/any.hpp"
#include <memory>
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
        conn->set_read_timeout(60);
        conn->startup();
    }
    void shutdown() { conn->shutdown(); }
    void set_on_message_cb(const std::function<void(const ConnectionPtr&, const std::string&)>& cb) { msg_cb_ = cb; }
    void set_on_close_cb(const std::function<void(const ConnectionPtr&)>& cb) { close_cb_ = cb; }
    std::string address() const { return conn->address(); }
    void write(const std::string& msg) { conn->write(msg); }
    void set_context(const boost::any& context) { this->context = context; }
    boost::any get_context() const { return context; }

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
    boost::any context;
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
struct Channel
{
   public:
    Channel(const std::string& id) : id_(id) {}
    ~Channel() {}

   public:
    bool empty() const { return conns_.empty(); }
    void add_conn(const ConnectionPtr& conn) { conns_.push_back(conn); }
    void del_conn(const ConnectionPtr& conn)
    {
        conns_.erase(std::remove_if(conns_.begin(), conns_.end(), [&conn](const auto& c) { return c == conn; }),
                     conns_.end());
    }
    void forward(const ConnectionPtr& conn, const std::string& msg)
    {
        for (const auto& c : conns_)
        {
            if (c == conn)
            {
                continue;
            }
            LOG_DEBUG << "forward channel " << id_ << " from " << conn->address() << " to " << c->address() << " --> "
                      << msg;
            c->write(msg);
        }
    }

   private:
    std::string id_;
    std::vector<ConnectionPtr> conns_;
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
        register_cmd_processor();
        catch_signal();
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
        unregister_cmd_processor();
    }

   private:
    void catch_signal()
    {
        auto signals = std::make_shared<boost::asio::signal_set>(io_context, SIGINT, SIGTERM);
        signals->async_wait(
            [this, signals](auto, auto)
            {
                io_context.stop();
                LOG_INFO << "s  ignal quit";
            });
    }

   private:
    void handshake(const ConnectionPtr& conn, const std::string& msg)
    {
        static const char kDelimiter = '$';
        auto pos = msg.find(kDelimiter);
        if (pos == std::string::npos)
        {
            return;
        }
        std::string id = msg.substr(0, pos);
        conn->set_context(id);
        conn->write(msg);
        add_conn(id, conn);
    }
    int peek_cmd(const std::string& msg)
    {
        int ch = msg[0];
        return ch;
    }
    void on_message(const ConnectionPtr& conn, const std::string& msg)
    {
        // LOG_DEBUG << conn->address() << " " << msg;
        if (conn->get_context().empty())
        {
            handshake(conn, msg);
        }
        else
        {
            process_message(conn, msg);
        }
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
        if (conn->get_context().empty())
        {
            return;
        }
        std::string id = boost::any_cast<std::string>(conn->get_context());
        del_conn(id, conn);
    }
    void process_message(const ConnectionPtr& conn, const std::string& msg)
    {
        std::string id = boost::any_cast<std::string>(conn->get_context());
        int cmd = peek_cmd(msg);
        auto it = std::find_if(proc.begin(), proc.end(), [cmd](auto& c) { return c.cmd == cmd; });
        if (it == proc.end())
        {
            return;
        }

        it->process(conn, msg);
    }

    void register_cmd_processor()
    {
        auto fn = [this](const ConnectionPtr& conn, const std::string& msg)
        {
            LOG_DEBUG << conn->address() << " --> 1 --> " << msg;
            std::string id = boost::any_cast<std::string>(conn->get_context());
            forward(id, conn, msg);
        };
        proc.push_back({'1', fn});
        proc.push_back({'2', [](const ConnectionPtr& conn, const std::string& msg)
                        {
                            LOG_DEBUG << conn->address() << " --> 2 --> " << msg;

                            conn->write(msg);
                        }});
        proc.push_back({'3', [](const ConnectionPtr& conn, const std::string& msg)
                        {
                            LOG_DEBUG << conn->address() << " --> 3 --> " << msg;
                            conn->write(msg);
                        }});
    }
    void unregister_cmd_processor() { proc.clear(); }

   private:
    void del_conn(const std::string& channel_id, const ConnectionPtr& conn)
    {
        auto ch_it = channels_.find(channel_id);
        if (ch_it == channels_.end())
        {
            return;
        }
        LOG_DEBUG << "del conn " << conn->address() << " from channel " << channel_id;
        ch_it->second.del_conn(conn);
        if (ch_it->second.empty())
        {
            channels_.erase(ch_it);
            LOG_DEBUG << "channel " << channel_id << " empty closed";
        }
    }
    void add_conn(const std::string& channel_id, const ConnectionPtr& conn)
    {
        LOG_DEBUG << "add conn " << conn->address() << " to channel " << channel_id;
        auto ch_it = channels_.find(channel_id);
        if (ch_it == channels_.end())
        {
            Channel ch(channel_id);
            ch.add_conn(conn);
            channels_.insert(std::make_pair(channel_id, ch));
        }
        else
        {
            ch_it->second.add_conn(conn);
        }
    }
    void forward(const std::string& channel_id, const ConnectionPtr& conn, const std::string& msg)
    {
        auto ch_it = channels_.find(channel_id);
        if (ch_it == channels_.end())
        {
            return;
        }

        ch_it->second.forward(conn, msg);
    }

   private:
    struct CmdProcess
    {
        int cmd;
        std::function<void(const ConnectionPtr&, const std::string&)> process;
    };

   private:
    std::vector<CmdProcess> proc;
    std::map<std::string, Channel> channels_;
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
