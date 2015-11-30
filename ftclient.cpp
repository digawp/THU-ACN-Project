// Source: https://onegazhang.wordpress.com/2009/09/22/file-transfer-over-asynchronous-tcp-connection-via-boost-asio/
// send a file to a tcp server via boost.asio library

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>

using boost::asio::ip::tcp;

class async_tcp_client
{
public:
    async_tcp_client(boost::asio::io_service& io_service,
        const std::string& server, std::vector<boost::filesystem::path> file_list)
    : resolver_(io_service),
    socket_(io_service),
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
    std::vector<boost::filesystem::path> file_queue;

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
                    boost::asio::placeholders::error, endpoint_iterator));
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
            std::shared_ptr<std::ifstream> filev = open_file(file_queue.back());
            file_queue.pop_back();

            // Create more connections for other files
            if (!file_queue.empty())
            {
                socket_.async_connect(*endpoint_iterator,
                    boost::bind(&async_tcp_client::handle_connect, this,
                        boost::asio::placeholders::error, endpoint_iterator));
            }

            // The connection was successful. Send the request.
            boost::asio::async_write(socket_, request_buf,
                boost::bind(&async_tcp_client::handle_write_file, this,
                    boost::asio::placeholders::error, filev));
        }
        else if (++endpoint_iterator != tcp::resolver::iterator())
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

    void handle_write_file(const boost::system::error_code& err,
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

                boost::asio::async_write(socket_,
                    boost::asio::buffer(buf.c_array(), source_file->gcount()),
                    boost::bind(&async_tcp_client::handle_write_file, this,
                        boost::asio::placeholders::error, source_file));
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

    std::shared_ptr<std::ifstream> open_file(boost::filesystem::path& file_path)
    {
        std::shared_ptr<std::ifstream> ret =
            std::make_shared<std::ifstream>(file_path.c_str(), std::ios_base::binary | std::ios_base::ate);
        if (!ret->good())
        {
            std::cout << "failed to open " << file_path.c_str() << std::endl;
            std::exit(1);
        }

        size_t file_size = ret->tellg();
        ret->seekg(0);

        std::cout << "File size: " << file_size << std::endl;

        // first send file name and file size to server
        std::ostream request_stream(&request_buf);
        request_stream << file_path << "\n"
            << file_size << "\n\n";
        std::cout << "request size:" << request_buf.size() << std::endl;

        return ret;
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
