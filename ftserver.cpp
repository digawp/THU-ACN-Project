// Source: https://onegazhang.wordpress.com/2009/09/22/file-transfer-over-asynchronous-tcp-connection-via-boost-asio/
//receive a file from socket client via boost.asio
#include <iostream>
#include <string>
#include <fstream>
#include <memory>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/filesystem.hpp>
#include <boost/current_function.hpp>

namespace util
{
    void print_error(const std::string& func_name, const std::string& err_msg)
    {
        std::printf("Error in %s: %s.\n", func_name.c_str(), err_msg.c_str());
    }
}

class async_tcp_conn: public std::enable_shared_from_this<async_tcp_conn>
{
public:
    async_tcp_conn(boost::asio::io_service& io_service)
    : socket_(io_service)
    {
    }

    void start(boost::filesystem::path file_path)
    {
        open_file(file_path);
        create_send_info(file_path);
        boost::asio::async_write(socket_, request_buf,
                boost::bind(&async_tcp_conn::handle_write_file,
                    shared_from_this(),
                    boost::asio::placeholders::error));
    }

    void notify_done()
    {
        create_send_info();
        boost::asio::write(socket_, request_buf);
        return;
    }

    boost::asio::ip::tcp::socket& get_socket() { return socket_; }

private:
    boost::asio::streambuf request_buf;
    std::ifstream source_file;
    boost::asio::ip::tcp::socket socket_;
    boost::array<char, 40960> buf;

    void handle_write_file(const boost::system::error_code& err)
    {
        if (!err)
        {
            if (!source_file.eof())
            {
                source_file.read(buf.c_array(), (std::streamsize)buf.size());

                if (source_file.gcount() <= 0)
                {
                    std::cout << "read file error " << std::endl;
                    std::cout << "gcount: " << source_file.gcount() << std::endl;
                    return;
                }

                // std::cout << "send " <<source_file.gcount()<<" bytes, total:" << source_file.tellg() << " bytes.\n";
                boost::asio::async_write(socket_,
                    boost::asio::buffer(buf.c_array(), source_file.gcount()),
                    boost::bind(&async_tcp_conn::handle_write_file,
                        shared_from_this(),
                        boost::asio::placeholders::error));
            }
            else
            {
                return;
            }
        }
        else
        {
            util::print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    void open_file(const boost::filesystem::path& file_path)
    {
        source_file.open(file_path.string(), std::ios_base::binary);

        if (!source_file)
        {
            std::cout << "failed to open " << file_path.string() << std::endl;
            std::exit(1);
        }
    }

    void create_send_info()
    {
        std::ostream request_stream(&request_buf);
        request_stream << "\0\0\n0\n\n";
        std::cout << "request size:" << request_buf.size() << std::endl;
    }

    void create_send_info(const boost::filesystem::path& file_path)
    {
        // first send file name and file size to server
        std::ostream request_stream(&request_buf);
        request_stream << file_path.string() << "\n"
            << boost::filesystem::file_size(file_path) << "\n\n";
        std::cout << "request size:" << request_buf.size() << std::endl;
    }
};

class async_tcp_server : private boost::noncopyable
{
public:
    typedef boost::asio::ip::tcp tcp;
    typedef std::shared_ptr<async_tcp_conn> ptr_async_tcp_conn;

    async_tcp_server(unsigned short port,
        std::vector<boost::filesystem::path> v)
    : acceptor_(io_service_, tcp::endpoint(tcp::v4(), port), true),
    file_list(std::move(v))
    {
        start_accept();
        io_service_.run();
    }

    ~async_tcp_server()
    {
        io_service_.stop();
    }

private:
    boost::asio::io_service io_service_;
    tcp::acceptor acceptor_;
    std::vector<boost::filesystem::path> file_list;

    void start_accept()
    {
        ptr_async_tcp_conn new_connection_(new async_tcp_conn(io_service_));
        acceptor_.async_accept(new_connection_->get_socket(),
            boost::bind(&async_tcp_server::handle_accept, this, new_connection_,
                boost::asio::placeholders::error));
    }

    void handle_accept(ptr_async_tcp_conn current_connection, const boost::system::error_code& e)
    {
        std::cout << __FUNCTION__ << " " << e << ", " << e.message()<<std::endl;
        if (!e && !file_list.empty())
        {
            current_connection->start(file_list.back());
            file_list.pop_back();
        }
        else if (!e && file_list.empty())
        {
            current_connection->notify_done();
        }
        else
        {
            util::print_error(BOOST_CURRENT_FUNCTION, e.message());
        }
        start_accept();
    }
};

unsigned short tcp_port = 1234;
std::string dir = "./Desktop/ServerFiles/";

int main(int argc, char* argv[])
{
    try
    {
        using namespace boost::filesystem;

        std::cout <<argv[0] << " listen on port " << tcp_port << std::endl;

        std::vector<path> file_list;
        path path_dir(dir);
        std::copy(recursive_directory_iterator(path_dir),
                recursive_directory_iterator(),
                back_inserter(file_list));

        async_tcp_server tcp_server(tcp_port, file_list);
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
