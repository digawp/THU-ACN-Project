// Source: https://onegazhang.wordpress.com/2009/09/22/file-transfer-over-asynchronous-tcp-connection-via-boost-asio/
// send a file to a tcp server via boost.asio library

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

#include <cstdio>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/current_function.hpp>
#include <boost/filesystem.hpp>

std::string server = "127.0.0.1:1234";
const std::string parent_dir = strcat(getenv("HOME"), "/Desktop/ClientFiles");

namespace util
{
    void print_error(const std::string& func_name, const std::string& err_msg)
    {
        std::printf("Error in %s: %s\n", func_name.c_str(), err_msg.c_str());
    }
}

using boost::asio::ip::tcp;

class async_tcp_client
{
public:
    async_tcp_client(const std::string& server);

    void notify();

private:
    boost::asio::io_service io_service;
    std::string server_ip;
    std::string server_port;

    // Starts a chain of callbacks which creates a connection between the client and the server
    void create_connection();
};

// Inherit from std::enable_shared_from_this<> because it might be destructed
// before the async callback returns
class tcp_client_conn : public std::enable_shared_from_this<tcp_client_conn>
{
public:
    tcp_client_conn(boost::asio::io_service& io_service, async_tcp_client* tcp_client)
    :resolver_(io_service), socket_(io_service), file_size(0)
    {
        parent = tcp_client;
    }

    void start_connection(const std::string server_ip, const std::string server_port)
    {
        tcp::resolver::query query(server_ip, server_port);

        resolver_.async_resolve(query,
            boost::bind(&tcp_client_conn::handle_resolve, shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::iterator));
    }

private:
    async_tcp_client* parent;

    std::size_t file_size;
    tcp::resolver resolver_;
    tcp::socket socket_;
    boost::array<char, 1024> buf;
    boost::asio::streambuf request_buf;
    std::ofstream output_file;

    // The callback function upon resolving the ip query
    void handle_resolve(const boost::system::error_code& err,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            // Attempt a connection to the first endpoint in the list. Each endpoint
            // will be tried until we successfully establish a connection.
            tcp::endpoint endpoint = *endpoint_iterator;

            socket_.async_connect(endpoint,
                boost::bind(&tcp_client_conn::handle_connect, shared_from_this(),
                    boost::asio::placeholders::error, endpoint_iterator));
        }
        else
        {
            util::print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    // The callback function upon connection attempt to server
    void handle_connect(const boost::system::error_code& err,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            async_read_until(socket_,
                request_buf, "\n\n",
                boost::bind(&tcp_client_conn::handle_read_request, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred)
                );
        }
        else if (++endpoint_iterator != tcp::resolver::iterator())
        {
            // The connection failed. Try the next endpoint in the list.
            socket_.close();
            tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(endpoint,
                boost::bind(&tcp_client_conn::handle_connect, shared_from_this(),
                    boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            util::print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    void handle_read_request(const boost::system::error_code& err,
        std::size_t bytes_transferred)
    {
        if (err)
        {
            return util::print_error(BOOST_CURRENT_FUNCTION, err.message());
        }

        std::istream request_stream(&request_buf);
        std::string file_path;

        request_stream >> file_path;
        request_stream >> file_size;
        request_stream.read(buf.c_array(), 2); // eat the "\n\n"

        // no more file to be received
        if (file_path == "\0\0")
        {
            std::cout << "No more files to receive" << std::endl;
            return;
        }

        modify_path_to_fulfil_reqmts(file_path);
        create_missing_directories(file_path);

        output_file.open(file_path.c_str(), std::ios_base::binary);

        if (!output_file)
        {
            std::cout << "failed to open " << file_path << std::endl;
            return;
        }

        // write extra bytes to file, if any
        do
        {
            request_stream.read(buf.c_array(), (std::streamsize)buf.size());
            output_file.write(buf.c_array(), request_stream.gcount());
        } while (request_stream.gcount()>0);

        async_read(socket_, boost::asio::buffer(buf.c_array(), buf.size()),
            boost::bind(&tcp_client_conn::handle_read_file_content,
                shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
    }

    void handle_read_file_content(const boost::system::error_code& err, std::size_t bytes_transferred)
    {
        if (bytes_transferred > 0 || err == boost::asio::error::eof)
        {
            output_file.write(buf.c_array(), (std::streamsize)bytes_transferred);

            // end of file reached
            if (output_file.tellp() >= (std::streamsize)file_size)
            {
                parent->notify();
                return;
            }
        }

        if (err)
        {
            return util::print_error(BOOST_CURRENT_FUNCTION, err.message());
        }

        // recurse
        async_read(socket_, boost::asio::buffer(buf.c_array(), buf.size()),
            boost::bind(&tcp_client_conn::handle_read_file_content,
                shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
    }

    void modify_path_to_fulfil_reqmts(std::string& path)
    {
        path = parent_dir + path;
    }

    void create_missing_directories(const std::string& path)
    {
        // Find the last folder delimiter
        size_t pos = path.find_last_of('/');

        if (pos!= std::string::npos)
        {
            // Create the missing folders, ignore the file name
            boost::filesystem::create_directories(path.substr(0, pos));
        }
    }
};


async_tcp_client::async_tcp_client(const std::string& server)
{
    size_t pos = server.find(':');
    // no information regarding port number. Terminate.
    if (pos == std::string::npos)
    {
        std::cerr << "No port number specified." << std::endl;
        return;
    }

    server_port = server.substr(pos+1);
    server_ip = server.substr(0, pos);

    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    create_connection();
    io_service.run();
}

void async_tcp_client::notify()
{
    create_connection();
}

void async_tcp_client::create_connection()
{
    std::shared_ptr<tcp_client_conn> conn =
        std::make_shared<tcp_client_conn>(io_service, this);
    conn->start_connection(server_ip, server_port);
}

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        server = argv[1];
    }

    try
    {
        async_tcp_client client(server);
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    std::cout << "receive file completed successfully.\n";

    return 0;
}
