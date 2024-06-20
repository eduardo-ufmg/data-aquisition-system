#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>

const std::string field_delimiter = "|";
const std::string sensor_first_field = "LOG";
const std::string client_first_field = "GET";

using boost::asio::ip::tcp;

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

class session : public std::enable_shared_from_this<session> {
public:

  session(tcp::socket socket) : socket_(std::move(socket)) {}

  void start() {
    read_message();
  }

private:
  void read_message() {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t length) {
          if (!ec) {
            std::istream is(&buffer_);
            std::string message(std::istreambuf_iterator<char>(is), {});
            std::cout << "Received: " << message << std::endl;

            std::size_t pos = message.find(field_delimiter);

            if (pos == std::string::npos) {
              std::cerr << "Invalid message format" << std::endl;
              return;
            }

            std::string first_field = message.substr(0, pos);
            message = message.substr(pos + 1);

            if (first_field == sensor_first_field) {
              store_sensor_data(message);
            } else if (first_field == client_first_field) {
              answer_client(message);
            } else {
              std::cerr << "Invalid message format" << std::endl;
              return;
            }

          }
        });
  }

  void answer_client(std::string& message) {

    std::size_t pos = message.find(field_delimiter);

    if (pos == std::string::npos) {
      std::cerr << "Invalid message format" << std::endl;
      return;
    }

    std::string sensor_id = message.substr(0, pos);
    message = message.substr(pos + 1);

    

    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer("message"),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_message();
          }
        });
  }

  void store_sensor_data(std::string& message) {

  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
};

class server {
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    accept();
  }

private:
  void accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
          if (!ec) {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}
