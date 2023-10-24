#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>

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

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32]; 
    std::time_t timestamp; 
    double value;
};
#pragma pack(pop)

std::vector<LogRecord> logRecords;

std::vector<std::string> split(const std::string& input, char delimiter) {
    std::istringstream iss(input);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    read_message();
  }

private:
  void read_message()
  {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::istream is(&buffer_);
            std::string message(std::istreambuf_iterator<char>(is), {});
            std::vector<std::string> tokens = split(message, '|');

            if(tokens[0] == "LOG"){
                LogRecord logRecordAux;
                strcpy(logRecordAux.sensor_id, tokens[1].c_str());
                logRecordAux.timestamp = string_to_time_t(tokens[2]);
                logRecordAux.value = std::stod(tokens[3]);
                logRecords.push_back(logRecordAux);
            }
            else if(tokens[0] == "GET"){
                std::string sensor_id = tokens[1];
                int quantity = std::stoi(tokens[2]);

                std::string response = "";
                int count = 0;
                for(int i = logRecords.size() - 1; i >= 0; i--){
                    if(logRecords[i].sensor_id == sensor_id){
                        response += time_t_to_string(logRecords[i].timestamp) + "|" + std::to_string(logRecords[i].value) + ";";
                        count++;
                    }
                    if(count == quantity){
                        break;
                    }
                }
                if(count == 0){
                    response = "ERROR|INVALID_SENSOR_ID\r\n";
                }else{
                    response = std::to_string(count) + ";" + response + "\r\n";
                }
                write_message(response);
            }
            std::cout << "Received: " << message << std::endl;
            write_message(message);
          }
        });
  }

  void write_message(const std::string& message)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(message),
        [this, self, message](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_message();
          }
        });
  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    accept();
  }

private:
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}
