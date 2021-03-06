#include "../include/sender.hh"
#include "cppiperconfig.hh"
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glog/logging.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

void cppiper::Sender::run() {
  while (true) {
    std::unique_lock lk(lock);
    if (not(msg_ready or stop)) {
      DLOG(INFO) << "Waiting on sender request for pipe " << pipepath.filename()
                 << "...";
      msg_conditional.notify_one();
      msg_conditional.wait(lk, [&]() { return msg_ready or stop; });
    }
    statuscode = 0;
    if (stop) {
      DLOG(INFO) << "Breaking from sender loop for pipe "
                 << pipepath.filename();
      break;
    }
    DLOG(INFO) << "Send request received for pipe " << pipepath.filename();
    std::stringstream ss;
    const int msg_size(buffer->size());
    ss << std::setfill('0') << std::setw(8) << std::hex << msg_size;
    DLOG(INFO) << "Sending message size bytes over pipe " << pipepath.filename()
               << "...";
    if (write(pipe_fd, ss.str().c_str(), 8) < 0) {
      LOG(ERROR) << "Failed to send message size bytes over pipe "
                 << pipepath.filename() << ", " << errno;
      statuscode = errno;
      msg_ready = false;
      lk.unlock();
      continue;
    }
    DLOG(INFO) << "Sending message bytes over pipe " << pipepath.filename()
               << "...";
    int bytes_written(0);
    int total_bytes_written(0);
    while ((bytes_written = write(
                pipe_fd, &buffer->front() + total_bytes_written,
                std::min(msg_size - total_bytes_written, buffering_limit))) >
               0 and
           (msg_size - (total_bytes_written += bytes_written) > 0)) {
      if (bytes_written < 0) {
        LOG(ERROR) << "Failed to send message bytes over pipe "
                   << pipepath.filename() << ", " << errno;
        statuscode = errno;
        msg_ready = false;
        lk.unlock();
        continue;
      }
    };
    msg_ready = false;
    lk.unlock();
  }
}

cppiper::Sender::Sender(const std::string name,
                        const std::filesystem::path pipepath)
    : name(name), pipepath(std::filesystem::absolute(pipepath)),
      buffer(nullptr), buffering_limit(65536), statuscode(0), pipe_fd(-1),
      msg_ready(false), stop(false), lock{}, msg_conditional{} {
  DLOG(INFO) << "Initialising sender thread for pipe " << pipepath.filename();
  int retcode;
  if (not std::filesystem::exists(pipepath)) {
    DLOG(INFO) << "Pipe " << pipepath << " does not exist, creating...";
    retcode = mkfifo(pipepath.c_str(), 00666);
    if (retcode == -1) {
      LOG(ERROR) << "Failed to open sender pipe " << pipepath << ", " << errno;
      statuscode = errno;
      return;
    }
  } else if (not std::filesystem::is_fifo(pipepath)) {
    LOG(ERROR) << "File at provided sender path " << pipepath
               << " is not a fifo pipe"
               << ", " << errno;
    statuscode = 95;
    return;
  }
  DLOG(INFO) << "Opening sender end of pipe " << pipepath << "...";
  pipe_fd = open(pipepath.c_str(), O_WRONLY | O_APPEND);
  if (pipe_fd == -1) {
    LOG(ERROR) << "Failed to open sender pipe " << pipepath << ", " << errno;
    statuscode = errno;
    return;
  }
  DLOG(INFO) << "Entering sender loop for pipe " << pipepath.filename()
             << "...";
  thread = std::thread(&Sender::run, this);
  LOG(INFO) << "Constructed sender instance " << name << " with pipe "
            << this->pipepath.filename();
}

int cppiper::Sender::get_status_code(void) const { return statuscode; };

bool cppiper::Sender::send(const std::string &msg) {
  LOG(INFO) << "Sending message on sender instance " << name;
  if (not thread.joinable() or stop) {
    LOG(WARNING)
        << "Attempt to send message on non-running sender instance for " << name
        << ", " << errno;
    return false;
  }
  {
    std::lock_guard lk(lock);
    buffer = &msg;
    msg_ready = true;
  }
  std::unique_lock lk(lock);
  msg_conditional.notify_one();
  if (msg_ready)
    msg_conditional.wait(lk, [&]() { return not msg_ready; });
  if (statuscode == 0)
    LOG(INFO) << "Message sent on sender instance " << name;
  else
    LOG(ERROR) << "Message failed to send on sender instance " << name << ", "
               << errno;
  return statuscode == 0;
}

bool cppiper::Sender::terminate(void) {
  DLOG(INFO) << "Terminating sender instance " << name << "...";
  if (not thread.joinable() or stop) {
    return true;
  }
  {
    std::lock_guard lk(lock);
    stop = true;
  }
  msg_conditional.notify_one();
  DLOG(INFO) << "Joining thread for sender instance " << name << "...";
  thread.join();
  if (close(pipe_fd) < 0) {
    LOG(ERROR) << "Failed to close sender end for pipe " << pipepath.filename()
               << ", " << errno;
    statuscode = 6;
  } else
    DLOG(INFO) << "Closed sender end for pipe " << pipepath.filename();
  DLOG(INFO) << "Joined thread for sender instance " << name;
  LOG(INFO) << "Terminated sender instance " << name;
  return true;
}

std::filesystem::path cppiper::Sender::get_pipe(void) const { return pipepath; }
