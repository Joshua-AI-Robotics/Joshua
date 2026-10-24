#include "ros2/logging.h"

#include <glog/logging.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"
#include "rcutils/logging.h"

namespace {
struct Record {
  std::string file;
  size_t line;
  std::string name;
  std::string message;
};
std::vector<Record> records;
void Capture(const rcutils_log_location_t* location,
             int,
             const char* name,
             rcutils_time_point_value_t,
             const char* format,
             va_list* arguments) {
  char buffer[1024];
  std::vsnprintf(buffer, sizeof(buffer), format, *arguments);
  records.push_back(
      {location ? location->file_name : "", location ? location->line_number : 0, name, buffer});
}
class LoggingTest : public testing::TestWithParam<config::General::Ros2LogMode> {
 protected:
  void SetUp() override {
    if (const char* directory = std::getenv("TEST_TMPDIR"))
      ASSERT_EQ(setenv("ROS_LOG_DIR", directory, 1), 0);
    char program[] = "logging_test";
    char ros_args[] = "--ros-args";
    char* arguments[] = {program, ros_args, nullptr};
    // Also exercises adding BOTH's console flag to an open ROS argument group.
    ros2_utils::InitializeRosLogging(2, arguments, GetParam());
    node = std::make_shared<rclcpp::Node>("logging_test");
    ros2_utils::SetLogNodeName(node->get_logger().get_name());
    saved_handler = rcutils_logging_get_output_handler();
    rcutils_logging_set_output_handler(Capture);
    records.clear();
  }
  void TearDown() override {
    rcutils_logging_set_output_handler(saved_handler);
    node.reset();
    rclcpp::shutdown();
  }
  std::shared_ptr<rclcpp::Node> node;
  rcutils_logging_output_handler_t saved_handler;
};
TEST_P(LoggingTest, RoutesOncePerBackendAndPreservesCallSite) {
  testing::internal::CaptureStderr();
  const auto line = __LINE__ + 1;
  JOSHUA_LOG(ERROR) << "route-marker " << 42;
  const auto output = testing::internal::GetCapturedStderr();
  const bool ros = GetParam() != config::General::ROS2_LOG_GLOG;
  const bool glog = GetParam() != config::General::ROS2_LOG_ROS;
  ASSERT_EQ(records.size(), ros ? 1u : 0u);
  if (ros) {
    EXPECT_EQ(records[0].file, __FILE__);
    EXPECT_EQ(records[0].line, line);
    EXPECT_EQ(records[0].name, "logging_test");
    EXPECT_EQ(records[0].message, "route-marker 42");
  }
  EXPECT_EQ(output.find("route-marker 42") != std::string::npos, glog);
}
TEST_P(LoggingTest, RosSeverityFiltering) {
  ASSERT_EQ(rcutils_logging_set_logger_level("logging_test", RCUTILS_LOG_SEVERITY_ERROR),
            RCUTILS_RET_OK);
  JOSHUA_LOG(INFO) << "filtered-from-ros";
  EXPECT_TRUE(records.empty());
  ASSERT_EQ(rcutils_logging_set_logger_level("logging_test", RCUTILS_LOG_SEVERITY_INFO),
            RCUTILS_RET_OK);
}
INSTANTIATE_TEST_SUITE_P(Modes,
                         LoggingTest,
                         testing::Values(config::General::ROS2_LOG_ROS,
                                         config::General::ROS2_LOG_GLOG,
                                         config::General::ROS2_LOG_BOTH));
TEST(LoggingConfigTest, DefaultsToBoth) {
  EXPECT_EQ(config::General().ros2_log_mode(), config::General::ROS2_LOG_BOTH);
}
}  // namespace
