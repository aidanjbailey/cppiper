#ifndef SENDER_HH_
#define SENDER_HH_
#include <barrier>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <spdlog/logger.h>
#include <string>
#include <thread>
#include <vector>
//#include <spdlog/spdlog.h>
#include <spdlog/spdlog.h>

namespace cppiper {

class Sender {
private:
  std::thread thread;
  //std::shared_ptr<spdlog::logger> logger;// = spdlog::basic_logger_mt("sender_logger", "logs/basic-log.txt");
  std::barrier<> processed_barrier;
  std::condition_variable msg_conditional;
  std::mutex lock;
  bool stop;
  bool msg_ready;
  std::vector<char> buffer;
  std::string pipepath;
  int statuscode;
  std::string name;


  const static void sender(const std::string pipepath, std::vector<char> &buffer,
                           int &statuscode, bool &msg_ready, const bool &stop,
                           std::mutex &lock,
                           std::condition_variable &msg_conditional,
                           std::barrier<> &processed_barrier);

public:
  Sender(void) = delete;
  Sender(std::string name, std::string pipepath);
  const int get_status_code(void);
  const bool send(const std::vector<char> &msg);
  const bool terminate(void);
};

} // namespace cppiper

#endif // SENDER_HH_
