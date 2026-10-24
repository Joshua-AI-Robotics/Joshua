#pragma once

#include <sstream>
#include <string>

#include "config/proto/config.pb.h"

namespace ros2_utils {

enum class LogSeverity { DEBUG, INFO, WARNING, ERROR, FATAL };

// Joshua runs one node per process. Configure before starting worker threads.
void InitializeLogging(const char* program);
void InitializeRosLogging(int argc,
                          char* argv[],
                          config::General::Ros2LogMode mode = config::General::ROS2_LOG_BOTH);
void SetLogNodeName(const std::string& name);

class NodeLogMessage {
 public:
  NodeLogMessage(LogSeverity severity, const char* file, int line, const char* function);
  ~NodeLogMessage() noexcept;
  std::ostream& stream() {
    return message_;
  }

 private:
  LogSeverity severity_;
  const char* file_;
  int line_;
  const char* function_;
  std::ostringstream message_;
};

}  // namespace ros2_utils

#define JOSHUA_LOG(severity)                                                                      \
  ::ros2_utils::NodeLogMessage(::ros2_utils::LogSeverity::severity, __FILE__, __LINE__, __func__) \
      .stream()
