// Source: https://onegazhang.wordpress.com/2009/09/22/file-transfer-over-asynchronous-tcp-connection-via-boost-asio/
// send a file to a tcp server via boost.asio library

#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <fstream>
#include <sstream>
#include <ctime>

using boost::asio::ip::tcp;

class async_tcp_client
{
public:
    async_tcp_client(boost::asio::io_service& io_service,
        const std::string& server, const std::string& path)
    : resolver_(io_service),
    socket_(io_service)
    {
        size_t pos = server.find(':');
        // no information regarding port number. Terminate.
        if (pos == std::string::npos)
        {
            std::cerr << "No port number specified." << std::endl;
            return;
        }

        std::string port_string = server.substr(pos+1);
        std::string server_ip_or_host = server.substr(0, pos);

        source_file.open(path.c_str(), std::ios_base::binary | std::ios_base::ate);
        if (!source_file)
        {
            std::cout << "failed to open " << path << std::endl;
            return ;
        }

        size_t file_size = source_file.tellg();
        source_file.seekg(0);

        std::cout << "File size: " << file_size << std::endl;

        // first send file name and file size to server
        std::ostream request_stream(&request_buf);
        request_stream << path << "\n"
            << file_size << "\n\n";
        std::cout << "request size:" << request_buf.size() << std::endl;

        // Start an asynchronous resolve to translate the server and service names
        // into a list of endpoints.
        tcp::resolver::query query(server_ip_or_host, port_string);
        resolver_.async_resolve(query,
            boost::bind(&async_tcp_client::handle_resolve, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::iterator));
    }

private:
    tcp::resolver resolver_;
    tcp::socket socket_;
    boost::array<char, 1024> buf;
    boost::asio::streambuf request_buf;
    std::ifstream source_file;

    void handle_resolve(const boost::system::error_code& err,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            // Attempt a connection to the first endpoint in the list. Each endpoint
            // will be tried until we successfully establish a connection.
            tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(endpoint,
                boost::bind(&async_tcp_client::handle_connect, this,
                    boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }

    void handle_connect(const boost::system::error_code& err,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            // The connection was successful. Send the request.
            boost::asio::async_write(socket_, request_buf,
                boost::bind(&async_tcp_client::handle_write_file, this,
                    boost::asio::placeholders::error));
        }
        else if (endpoint_iterator != tcp::resolver::iterator())
        {
            // The connection failed. Try the next endpoint in the list.
            socket_.close();
            tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(endpoint,
                boost::bind(&async_tcp_client::handle_connect, this,
                    boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }

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

                std::cout << "send " <<source_file.gcount()<<" bytes, total:" << source_file.tellg() << " bytes.\n";

                boost::asio::async_write(socket_,
                    boost::asio::buffer(buf.c_array(), source_file.gcount()),
                    boost::bind(&async_tcp_client::handle_write_file, this,
                        boost::asio::placeholders::error));
            }
            else
            {
                return;
            }
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }
};

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <server-address>:<port-no> <file path>" << std::endl;

        #ifdef _WIN32
        std::cerr << "sample: " << argv[0] << " 127.0.0.1:1234 c:\\tmp\\a.txt" << std::endl;
        #else
        std::cerr << "sample: " << argv[0] << " 127.0.0.1:1234 c:/tmp/a.txt" << std::endl;
        #endif

        return __LINE__;
    }

    std::time_t time_begin = std::time(NULL);
    try
    {
        boost::asio::io_service io_service;
        async_tcp_client client(io_service, argv[1], argv[2]);
        io_service.run();

        std::cout << "send file " << argv[2] << " completed successfully.\n";
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    std::time_t time_end = std::time(NULL);
    std::cout << "Time elapsed: " << std::difftime(time_end, time_begin) << std::endl;

    return 0;
}
