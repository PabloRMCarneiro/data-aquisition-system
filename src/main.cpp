#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <map>
#include <unordered_map>

using boost::asio::ip::tcp;

std::unordered_map<std::string, std::fstream> sensorFiles;

std::fstream &get_sensor_file(const std::string &sensor_id)
{
  const std::string filename = "../log_files/log_" + sensor_id + ".bin";

  auto &file = sensorFiles[sensor_id];
  if (!file.is_open())
  {
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file)
    {
      throw std::runtime_error("Failed to open sensor log file: " + filename);
    }
  }

  return file;
}

std::fstream logFile;

std::map<std::string, std::fstream> logFiles;
std::map<std::string, std::mutex> logFileMutexes;

void open_log_file_for_sensor(const std::string &sensor_id)
{
  std::lock_guard<std::mutex> lock(logFileMutexes[sensor_id]);
  auto &logFile = logFiles[sensor_id];
  if (!logFile.is_open())
  {
    std::string filename = "../log_files/log_" + sensor_id + ".bin";
    logFile.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!logFile.is_open())
    {
      logFile.clear();
      logFile.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
      if (!logFile.is_open())
      {
        std::cerr << "Failed to open log file for sensor " << sensor_id << std::endl;
        exit(1);
      }
    }
  }
}

std::time_t string_to_time_t(const std::string &time_string)
{
  std::tm tm = {};
  std::istringstream ss(time_string);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time)
{
  std::tm *tm = std::localtime(&time);
  std::ostringstream ss;
  ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
  return ss.str();
}

#pragma pack(push, 1)
struct LogRecord
{
  char sensor_id[32];
  std::time_t timestamp;
  double value;
};
#pragma pack(pop)

std::vector<std::string> split(const std::string &input, char delimiter)
{
  std::istringstream iss(input);
  std::string token;
  std::vector<std::string> tokens;

  while (std::getline(iss, token, delimiter))
  {
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

                                      if (tokens[0] == "LOG")
                                      {
                                        std::string sensor_id = tokens[1];
                                        open_log_file_for_sensor(sensor_id);

                                        std::lock_guard<std::mutex> lock(logFileMutexes[sensor_id]);
                                        LogRecord logRecordAux;
                                        strcpy(logRecordAux.sensor_id, tokens[1].c_str());
                                        logRecordAux.timestamp = string_to_time_t(tokens[2]);
                                        logRecordAux.value = std::stod(tokens[3]);
                                        logFiles[sensor_id].write(reinterpret_cast<char *>(&logRecordAux), sizeof(LogRecord));
                                        logFiles[sensor_id].flush();
                                      }
                                      else if (tokens[0] == "GET")
                                      {
                                        std::string sensor_id = tokens[1];
                                        int quantity = std::stoi(tokens[2]);

                                        std::fstream &logFile = get_sensor_file(sensor_id);

                                        logFile.clear(); // Limpar flags de erro
                                        logFile.seekg(0, std::ios::end);
                                        std::streampos fileSize = logFile.tellg();

                                        int totalRecords = fileSize / sizeof(LogRecord);
                                        int recordsToRead = std::min(quantity, totalRecords);

                                        if (recordsToRead > 0)
                                        {
                                          std::streampos readPosition = fileSize - static_cast<std::streamoff>(recordsToRead * static_cast<std::streamoff>(sizeof(LogRecord)));
                                          logFile.seekg(readPosition);

                                          std::string response = "";
                                          int count = 0;
                                          LogRecord logRecordAux;

                                          while (logFile.read(reinterpret_cast<char *>(&logRecordAux), sizeof(LogRecord)) && count < quantity)
                                          {
                                            response = time_t_to_string(logRecordAux.timestamp) + "|" + std::to_string(logRecordAux.value) + ";" + response;
                                            count++;
                                          }

                                          if (count == 0)
                                          {
                                            response = "ERROR|INVALID_SENSOR_ID\r\n";
                                          }
                                          else
                                          {
                                            response = std::to_string(count) + ";" + response + "\r\n";
                                          }
                                          write_message(response);
                                        }
                                        else
                                        {
                                          write_message("ERROR|NO_RECORDS\r\n");
                                        }
                                      }
                                      std::cout << "Received: " << message << std::endl;
                                      write_message(message);
                                    }
                                  });
  }

  void write_message(const std::string &message)
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
  server(boost::asio::io_context &io_context, short port)
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

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: async_tcp_echo_server <port>\n";
    return 1;
  }

  try
  {
    boost::asio::io_context io_context;
    server s(io_context, std::atoi(argv[1]));
    io_context.run();
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  for (auto &pair : logFiles)
  {
    if (pair.second.is_open())
    {
      pair.second.close();
    }
  }

  return 0;
}
