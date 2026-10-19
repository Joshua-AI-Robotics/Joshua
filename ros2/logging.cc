#include "ros2/logging.h"

#include <glog/logging.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rcutils/logging.h"

namespace ros2_utils {
namespace {
struct LoggingState {
  std::mutex mutex;
  config::General::Ros2LogMode mode = config::General::ROS2_LOG_BOTH;
  std::string name = "joshua";
};
LoggingState& State() {
  static LoggingState state;
  return state;
}
int RosSeverity(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::DEBUG:
      return RCUTILS_LOG_SEVERITY_DEBUG;
    case LogSeverity::INFO:
      return RCUTILS_LOG_SEVERITY_INFO;
    case LogSeverity::WARNING:
      return RCUTILS_LOG_SEVERITY_WARN;
    case LogSeverity::ERROR:
      return RCUTILS_LOG_SEVERITY_ERROR;
    case LogSeverity::FATAL:
      return RCUTILS_LOG_SEVERITY_FATAL;
  }
  return RCUTILS_LOG_SEVERITY_ERROR;
}
int GlogSeverity(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::DEBUG:
    case LogSeverity::INFO:
      return google::GLOG_INFO;
    case LogSeverity::WARNING:
      return google::GLOG_WARNING;
    case LogSeverity::ERROR:
      return google::GLOG_ERROR;
    case LogSeverity::FATAL:
      return google::GLOG_FATAL;
  }
  return google::GLOG_ERROR;
}
}  // namespace

void InitializeLogging(const char* program) {
  if (!google::IsGoogleLoggingInitialized()) google::InitGoogleLogging(program);
  FLAGS_logtostderr = 1;
}

void InitializeRosLogging(int argc, char* argv[], config::General::Ros2LogMode mode) {
  if (!config::General::Ros2LogMode_IsValid(mode))
    throw std::invalid_argument("Unknown general.ros2_log_mode");
  InitializeLogging(argv[0]);
  {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.mode = mode;
  }
  // Keep the usual ROS handler and /rosout registration, avoiding custom sinks.
  // BOTH has one console writer (glog); ROS retains /rosout and file logging.
  std::vector<std::string> arguments(argv, argv + argc);
  if (mode == config::General::ROS2_LOG_BOTH) {
    bool in_ros_args = false;
    for (const auto& argument : arguments) {
      if (argument == "--ros-args") in_ros_args = true;
      if (argument == "--") in_ros_args = false;
    }
    if (in_ros_args) arguments.emplace_back("--");
    arguments.emplace_back("--ros-args");
    arguments.emplace_back("--disable-stdout-logs");
  }
  std::vector<const char*> pointers;
  for (const auto& argument : arguments) pointers.push_back(argument.c_str());
  pointers.push_back(nullptr);
  rclcpp::init(static_cast<int>(arguments.size()), pointers.data());
}

void SetLogNodeName(const std::string& name) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.name = name;
}

NodeLogMessage::NodeLogMessage(LogSeverity severity,
                               const char* file,
                               int line,
                               const char* function)
    : severity_(severity), file_(file), line_(line), function_(function) {}

NodeLogMessage::~NodeLogMessage() noexcept {
  try {
    config::General::Ros2LogMode mode;
    std::string name;
    {
      auto& state = State();
      std::lock_guard<std::mutex> lock(state.mutex);
      mode = state.mode;
      name = state.name;
    }
    const auto text = message_.str();
    const bool ros_available = rclcpp::ok() && g_rcutils_logging_initialized;
    const int ros_severity = RosSeverity(severity_);
    if (ros_available && mode != config::General::ROS2_LOG_GLOG &&
        (severity_ == LogSeverity::FATAL ||
         rcutils_logging_logger_is_enabled_for(name.c_str(), ros_severity))) {
      const rcutils_log_location_t location{function_, file_, static_cast<size_t>(line_)};
      rcutils_log(&location, ros_severity, name.c_str(), "%s", text.c_str());
    }
    // Startup/config-load failures and shutdown diagnostics must remain visible.
    const bool use_glog = mode != config::General::ROS2_LOG_ROS || !ros_available;
    if (use_glog && (severity_ != LogSeverity::DEBUG || FLAGS_v >= 1)) {
      google::LogMessage(file_, line_, GlogSeverity(severity_)).stream()
          << "[" << name << "] " << text;
    }
  } catch (...) {
    // Never throw from a logging temporary, including during stack unwinding.
    std::fputs("Joshua logging failed\n", stderr);
  }
  // ROS FATAL is normally nonterminating; Joshua FATAL always terminates,
  // regardless of the selected backend or severity filters.
  if (severity_ == LogSeverity::FATAL) std::abort();
}
}  // namespace ros2_utils
