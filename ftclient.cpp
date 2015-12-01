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

using boost::asio::ip::tcp;

class async_tcp_client
{
public:
    async_tcp_client(boost::asio::io_service& io_service,
        const std::string& server, std::vector<boost::filesystem::path> file_list)
    : resolver_(io_service),
    file_queue(std::move(file_list))
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

        // Start an asynchronous resolve to translate the server and service names
        // into a list of endpoints.
        connect(server_ip_or_host, port_string);
    }

private:
    tcp::resolver resolver_;
    boost::array<char, 1024> buf;
    boost::asio::streambuf request_buf;
    std::vector<boost::filesystem::path> file_queue;

    // Starts a chain of callbacks which creates a connection between the client and the server
    void connect(const std::string& server_ip, const std::string& server_port)
    {
        tcp::resolver::query query(server_ip, server_port);
        resolver_.async_resolve(query,
            boost::bind(&async_tcp_client::handle_resolve, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::iterator));
    }

    // The callback function upon resolving the ip query
    void handle_resolve(const boost::system::error_code& err,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            // Attempt a connection to the first endpoint in the list. Each endpoint
            // will be tried until we successfully establish a connection.
            tcp::endpoint endpoint = *endpoint_iterator;
            std::shared_ptr<tcp::socket> socketv =
                std::make_shared<tcp::socket>(resolver_.get_io_service());

            socketv->async_connect(endpoint,
                boost::bind(&async_tcp_client::handle_connect, this,
                    boost::asio::placeholders::error,
                    socketv, endpoint_iterator));
        }
        else
        {
            print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    // The callback function upon connection attempt to server
    void handle_connect(const boost::system::error_code& err,
        std::shared_ptr<tcp::socket> socketv,
        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            std::shared_ptr<std::ifstream> filev = open_file(file_queue.back());
            send_request(file_queue.back());
            file_queue.pop_back();

            // Create more connections for other files
            if (!file_queue.empty())
            {
                tcp::endpoint endpoint = *endpoint_iterator;
                connect(endpoint.address().to_string(), std::to_string(endpoint.port()));
            }

            // The connection was successful. Send the request.
            boost::asio::async_write(*socketv, request_buf,
                boost::bind(&async_tcp_client::handle_write_file, this,
                    boost::asio::placeholders::error, socketv, filev));
        }
        else if (++endpoint_iterator != tcp::resolver::iterator())
        {
            // The connection failed. Try the next endpoint in the list.
            socketv->close();
            tcp::endpoint endpoint = *endpoint_iterator;
            socketv->async_connect(endpoint,
                boost::bind(&async_tcp_client::handle_connect, this,
                    boost::asio::placeholders::error,
                    socketv, ++endpoint_iterator));
        }
        else
        {
            print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    void handle_write_file(const boost::system::error_code& err,
        std::shared_ptr<tcp::socket> socketv,
        std::shared_ptr<std::ifstream> source_file)
    {
        if (!err)
        {
            if (!source_file->eof())
            {
                source_file->read(buf.c_array(), (std::streamsize)buf.size());

                if (source_file->gcount() <= 0)
                {
                    std::cout << "read file error " << std::endl;
                    std::cout << "gcount: " << source_file->gcount() << std::endl;
                    return;
                }

                // std::cout << "send " <<source_file->gcount()<<" bytes, total:" << source_file->tellg() << " bytes.\n";
                boost::asio::async_write(*socketv,
                    boost::asio::buffer(buf.c_array(), source_file->gcount()),
                    boost::bind(&async_tcp_client::handle_write_file, this,
                        boost::asio::placeholders::error, socketv, source_file));
            }
            else
            {
                return;
            }
        }
        else
        {
            print_error(BOOST_CURRENT_FUNCTION, err.message());
        }
    }

    std::shared_ptr<std::ifstream> open_file(const boost::filesystem::path& file_path)
    {
        std::shared_ptr<std::ifstream> ret =
            std::make_shared<std::ifstream>(file_path.string(), std::ios_base::binary);

        if (!ret->good())
        {
            std::cout << "failed to open " << file_path.string() << std::endl;
            std::exit(1);
        }

        return ret;
    }

    void send_request(const boost::filesystem::path& file_path)
    {
        // first send file name and file size to server
        std::ostream request_stream(&request_buf);
        request_stream << file_path.string() << "\n"
            << boost::filesystem::file_size(file_path) << "\n\n";
        std::cout << "request size:" << request_buf.size() << std::endl;
    }

    void print_error(const std::string& func_name, const std::string& err_msg)
    {
        std::printf("Error in %s: %s\n", func_name.c_str(), err_msg.c_str());
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

    try
    {
        using namespace boost::filesystem;

        std::vector<path> file_list;
        path path_specified(argv[2]);

        if (is_regular_file(path_specified))
        {
            file_list.push_back(path_specified);
        }
        else if (is_directory(path_specified))
        {
            std::copy(directory_iterator(path_specified), directory_iterator(),
                back_inserter(file_list));
        }
        else
        {
            std::cerr << "Unknown type: " << argv[2] << std::endl;
            return 1;
        }

        boost::asio::io_service io_service;
        async_tcp_client client(io_service, argv[1], file_list);
        io_service.run();

        std::cout << "send file " << argv[2] << " completed successfully.\n";
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
