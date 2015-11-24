// Source: https://onegazhang.wordpress.com/2009/09/22/file-transfer-over-asynchronous-tcp-connection-via-boost-asio/
//receive a file from socket client via boost.asio
#include <iostream>
#include <string>
#include <fstream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>

unsigned short tcp_port = 1234;

class async_tcp_connection: public boost::enable_shared_from_this<async_tcp_connection>
{
public:
    async_tcp_connection(boost::asio::io_service& io_service)
    : socket_(io_service), file_size(0)
    {
    }

    void start()
    {
        std::cout << __FUNCTION__  << std::endl;
        async_read_until(socket_,
            request_buf, "\n\n",
            boost::bind(&async_tcp_connection::handle_read_request,
                shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)
            );
    }

    boost::asio::ip::tcp::socket& get_socket() { return socket_; }

private:
    boost::asio::streambuf request_buf;
    size_t file_size;
    std::ofstream output_file;
    boost::asio::ip::tcp::socket socket_;
    boost::array<char, 40960> buf;

    void handle_error(const std::string& function_name, const boost::system::error_code& err)
    {
        std::cout << __FUNCTION__ << " in " << function_name <<" due to " << err <<" " << err.message()<< std::endl;
    }

    void handle_read_request(const boost::system::error_code& err, std::size_t bytes_transferred)
    {
        if (err)
        {
            return handle_error(__FUNCTION__, err);
        }

        std::cout << __FUNCTION__ << "(" << bytes_transferred << ")"
            << ", in_avail=" << request_buf.in_avail()
            << ", size=" << request_buf.size()
            << ", max_size=" << request_buf.max_size() <<".\n";

        std::istream request_stream(&request_buf);
        std::string file_path;

        request_stream >> file_path;
        request_stream >> file_size;
        request_stream.read(buf.c_array(), 2); // eat the "\n\n"

        std::cout << file_path << " size is " << file_size << ", tellg=" << request_stream.tellg()<< std::endl;

        // Find the last folder delimiter
        #ifdef _WIN32
        size_t pos = file_path.find_last_of('\\');
        #else
        size_t pos = file_path.find_last_of('/');
        #endif

        // Folder delimiter found. Cut all the folder names and just preserve the file name.
        if (pos!= std::string::npos)
        {
            create_missing_directories(file_path.substr(0, pos));
            // file_path = file_path.substr(pos+1);
        }

        output_file.open(file_path.c_str(), std::ios_base::binary);

        if (!output_file)
        {
            std::cout << "failed to open " << file_path << std::endl;
            return;
        }

        // write extra bytes to file
        do
        {
            request_stream.read(buf.c_array(), (std::streamsize)buf.size());
            std::cout << __FUNCTION__ << " write " << request_stream.gcount() << " bytes.\n";
            output_file.write(buf.c_array(), request_stream.gcount());
        } while (request_stream.gcount()>0);

        async_read(socket_, boost::asio::buffer(buf.c_array(), buf.size()),
            boost::bind(&async_tcp_connection::handle_read_file_content,
                shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }

    void handle_read_file_content(const boost::system::error_code& err, std::size_t bytes_transferred)
    {
        if (bytes_transferred > 0)
        {
            output_file.write(buf.c_array(), (std::streamsize)bytes_transferred);
            // std::cout << __FUNCTION__ << " recv " << output_file.tellp() << " bytes."<< std::endl;

            // end of file reached
            if (output_file.tellp() >= (std::streamsize)file_size)
            {
                std:: cout << "End of file. Thread terminates..." << std::endl;
                return;
            }
        }

        if (err)
        {
            return handle_error(__FUNCTION__, err);
        }

        // recurse
        async_read(socket_, boost::asio::buffer(buf.c_array(), buf.size()),
            boost::bind(&async_tcp_connection::handle_read_file_content,
                shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }

    void create_missing_directories(const std::string& path)
    {
        boost::filesystem::create_directories(path);
    }
};

class async_tcp_server : private boost::noncopyable
{
public:
    typedef boost::shared_ptr<async_tcp_connection> ptr_async_tcp_connection;

    async_tcp_server(unsigned short port)
    : acceptor_(io_service_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port), true)
    {
        start_accept();
        io_service_.run();
    }

    void handle_accept(ptr_async_tcp_connection current_connection, const boost::system::error_code& e)
    {
        std::cout << __FUNCTION__ << " " << e << ", " << e.message()<<std::endl;
        if (!e)
        {
            current_connection->start();
        }
        start_accept();
    }

    ~async_tcp_server()
    {
        io_service_.stop();
    }

private:
    boost::asio::io_service io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;

    void start_accept()
    {
        ptr_async_tcp_connection new_connection_(new async_tcp_connection(io_service_));
        acceptor_.async_accept(new_connection_->get_socket(),
            boost::bind(&async_tcp_server::handle_accept, this, new_connection_,
                boost::asio::placeholders::error));
    }
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc==2)
        {
            tcp_port=atoi(argv[1]);
        }
        std::cout <<argv[0] << " listen on port " << tcp_port << std::endl;
        async_tcp_server *recv_file_tcp_server = new async_tcp_server(tcp_port);
        delete recv_file_tcp_server;
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
