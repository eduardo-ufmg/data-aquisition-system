#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <regex>
#include <fstream>

const std::string msg_delimiter      = "\r\n";
const std::string field_delimiter    =    "|";
const std::string sensor_first_field =  "LOG";
const std::string client_first_field =  "GET";

const std::regex client_pattern("[\x00-\x7F]{1,32}\|[0-9]+\r\n");
const std::regex sensor_pattern("[\x00-\x7F]{1,32}\|[0-9]+-[0-9]+-[0-9]+T[0-9]+:[0-9]+:[0-9]+\|[0-9]+.?[0-9]+\r\n");

using boost::asio::ip::tcp;

struct LogRecord {
  std::time_t timestamp;
  double value;
};

std::time_t string_to_time_t(const std::string& time_string);
std::string time_t_to_string(std::time_t time);
std::string extract_msg_field(std::string& message, const std::string& delimiter);

class session : public std::enable_shared_from_this<session> {
public:

  session(tcp::socket socket) : socket_(std::move(socket)) {}

  void start() {
    read_message();
  }

private:
  void read_message() {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, read_buffer_, msg_delimiter,
      [this, self](boost::system::error_code ec, std::size_t length) {
        if(!ec) {
          std::istream is(&read_buffer_);
          std::string message(std::istreambuf_iterator<char>(is), {});
          std::cout << "Received: " << message << std::endl;

          bool is_valid = false;

          std::string first_field = extract_msg_field(message, field_delimiter);

          if(first_field == sensor_first_field) {
            
            if(std::regex_match(message, sensor_pattern)) {
              is_valid = true;
              store_sensor_data(message);
            }

          } else if (first_field == client_first_field) {
            
            if(std::regex_match(message, client_pattern)) {
              is_valid = true;
              answer_client(message);
            }

          }

          read_message();

          if(!is_valid)
            goto invalid_message_format;

          return;

          invalid_message_format:
          std::cerr << "Invalid message format" << std::endl;

        }
      });
  }

  void answer_client(std::string& message) {

    std::string sensor_id = extract_msg_field(message, field_delimiter);

    std::string sample_quantity = extract_msg_field(message, msg_delimiter);

    auto self(shared_from_this());
    boost::asio::async_write(socket_, write_buffer_,
      [this, self](boost::system::error_code ec, std::size_t length) {});
  }

  void store_sensor_data(std::string& message) {

    std::string sensor_id = extract_msg_field(message, field_delimiter);

    std::string timestamp = extract_msg_field(message, field_delimiter);

    std::string value = extract_msg_field(message, msg_delimiter);

    LogRecord record = {string_to_time_t(timestamp), std::stod(value)};

    std::fstream file(sensor_id + ".dat", std::ios::out | std::fstream::binary | std::ios::app);

    if(!file.is_open()) {
      std::cerr << "Error opening file" << std::endl;
      return;
    }

    file.write((char*)&record, sizeof(LogRecord));

    file.close();

  }

  tcp::socket socket_;
  boost::asio::streambuf read_buffer_;
  boost::asio::streambuf write_buffer_;
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
          if(!ec) {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {

  int port;

  if(argc == 2) {
    port = std::atoi(argv[1]);
  } else {
    std::cerr << "Usage: chat_server <port>. 9000 will be used\r\n";
    port = 9000;
  }

  boost::asio::io_context io_context;

  server s(io_context, port);

  io_context.run();

  return 0;
}

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

std::string extract_msg_field(std::string& message, const std::string& delimiter) {
  std::size_t pos = message.find(delimiter);
  std::string field = message.substr(0, pos);
  message = message.substr(pos + 1);
  return field;
}