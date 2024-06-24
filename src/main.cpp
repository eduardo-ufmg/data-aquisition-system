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
const std::string entry_delimiter    =    ";";
const std::string sensor_first_field =  "LOG";
const std::string client_first_field =  "GET";
const std::string error_msg          =  "ERROR|INVALID_SENSOR_ID\r\n";

const std::regex client_pattern("[\\w\\s]{1,32}\\|[0-9]+\\r\\n");
const std::regex sensor_pattern("[\\w\\s]{1,32}\\|\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{6}\\|[-+]?[0-9]*\\.?[0-9]+\\r\\n");

using boost::asio::ip::tcp;

struct LogRecord {
  std::time_t timestamp;
  double value;
};

std::time_t string_to_time_t(const std::string& time_string);
std::string time_t_to_string(std::time_t time);
std::string extract_msg_field(std::string& message, const std::string& delimiter);
void string_to_buffer(const std::string& message, boost::asio::streambuf& buffer);

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

    std::fstream file(sensor_id + ".dat", std::fstream::in | std::fstream::binary);

    if(!file.is_open()) {
      string_to_buffer(error_msg, write_buffer_);
    } else {
      file.seekg(0, std::ios::end);
      std::streampos file_size = file.tellg();
      int entry_size = sizeof(LogRecord);
      int entry_qtty = file_size / entry_size;
      entry_qtty = std::min(std::stoi(sample_quantity), entry_qtty);

      LogRecord record;
      std::string response = std::to_string(entry_qtty) + entry_delimiter;

      for(int i = 1; i <= entry_qtty; i ++) {
        file.seekg(-i * entry_size, std::ios::end);
        file.read((char*)(&record), sizeof(LogRecord));
        response += time_t_to_string(record.timestamp) + field_delimiter
                    + std::to_string(record.value) + ((i == entry_qtty) ? msg_delimiter : entry_delimiter);
      }

      string_to_buffer(response, write_buffer_);

    }

    auto self(shared_from_this());
    boost::asio::async_write(socket_, write_buffer_,
      [this, self](boost::system::error_code ec, std::size_t length) {});
  }

  void store_sensor_data(std::string& message) {

    std::string sensor_id = extract_msg_field(message, field_delimiter);

    std::string timestamp = extract_msg_field(message, field_delimiter);

    std::string value = extract_msg_field(message, msg_delimiter);

    LogRecord record = {string_to_time_t(timestamp), std::stod(value)};

    std::fstream file(sensor_id + ".dat", std::fstream::out | std::fstream::binary | std::ios::app);

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

void string_to_buffer(const std::string& message, boost::asio::streambuf& buffer) {
  buffer.consume(buffer.size());
  std::ostream os(&buffer);
  os << message;
}