// Test-only ROS publishers and fake process facts. Never constructs a driver.
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

#include "node_generator/runtime_observation.h"
#include "rcl_interfaces/msg/log.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return 2;
  config::Config cfg;
  cfg.mutable_general()->set_name("observation-fixture");
  auto* sensor = cfg.mutable_robot()->mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("encoder");
  sensor->set_sensor_type(robot::perception::POSITION);
  sensor->mutable_sts3215_encoder_config()->set_board_name("declared_board_only");
  sensor->mutable_sts3215_encoder_config()->set_channel(3);
  sensor->mutable_node()->set_id(7);
  sensor->mutable_node()->set_node_type(ros2::node::POSITION_PUBLISHER);
  auto* pub = sensor->mutable_node()->add_publishers();
  pub->set_ros2_data_type(ros2::data_type::JOINT_STATE);
  pub->set_topic("/joshua_observation_fixture/position");
  auto prepared =
      node_generator::RuntimeObservation::Prepare(cfg, "fixture.pbtxt", argv[1], "encoder");
  if (!prepared.ok()) {
    std::cerr << prepared.status();
    return 3;
  }
  auto observer = std::move(*prepared);
  // All forks precede both contexts, just as in the launcher.
  pid_t child = fork();
  if (child == 0) {
    execl("/bin/sleep", "sleep", "1", nullptr);
    _exit(2);
  }
  std::vector<node_generator::ProcessFact> processes = {
      {static_cast<int>(child), 7, "position_publisher_node_7", "position_publisher"}};
  const char* rmw_env = std::getenv("RMW_IMPLEMENTATION");
  const std::string previous_rmw = rmw_env ? rmw_env : "";
  if (argc == 3) setenv("RMW_IMPLEMENTATION", "deliberately_unavailable_test_rmw", 1);
  observer->Start();
  if (previous_rmw.empty())
    unsetenv("RMW_IMPLEMENTATION");
  else
    setenv("RMW_IMPLEMENTATION", previous_rmw.c_str(), 1);
  auto context = std::make_shared<rclcpp::Context>();
  rclcpp::InitOptions init_options;
  init_options.auto_initialize_logging(false);
  init_options.shutdown_on_signal = false;
  context->init(0, nullptr, init_options);
  rclcpp::NodeOptions options;
  options.context(context).enable_rosout(false);
  auto source = std::make_shared<rclcpp::Node>("position_publisher_node_7", options);
  auto rogue = std::make_shared<rclcpp::Node>("unrelated_publisher", options);
  auto make_publisher = [&](auto node) {
    return node->template create_publisher<sensor_msgs::msg::JointState>(
        pub->topic(), rclcpp::QoS(1).best_effort().durability_volatile());
  };
  auto publisher = make_publisher(source);
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr other;
  auto log = source->create_publisher<rcl_interfaces::msg::Log>(
      "/rosout", rclcpp::QoS(32).best_effort().durability_volatile());
  std::string mode = "normal";
  for (int i = 0; i < 1800; ++i) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN)) {
      std::string next;
      char c;
      while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') next += c;
      if (next == "exit") break;
      mode = next;
      if (mode == "block_output") {
        unlink((std::string(argv[1]) + "/snapshot.json").c_str());
        mkdir((std::string(argv[1]) + "/snapshot.json").c_str(), 0700);
      }
      if (mode == "restore_output") rmdir((std::string(argv[1]) + "/snapshot.json").c_str());
      if (mode == "restart_silent") publisher.reset();
      if (mode == "missing") {
        publisher.reset();
        other.reset();
      } else if (mode == "wrong") {
        publisher.reset();
        if (!other) other = make_publisher(rogue);
      } else if (mode == "ambiguous") {
        if (!publisher) publisher = make_publisher(source);
        if (!other) other = make_publisher(rogue);
      } else {
        other.reset();
        if (!publisher) publisher = make_publisher(source);
      }
    }
    if (child > 0) {
      int status;
      if (waitpid(child, &status, WNOHANG) == child) {
        observer->RecordExit(processes.front(), status);
        processes.clear();
        child = -1;
        std::cout << "REAPED" << std::endl;
      }
    }
    if (publisher && mode != "silent" && mode != "restart_silent") {
      sensor_msgs::msg::JointState message;
      message.name = {"encoder"};
      message.position = {0.75};
      message.header.stamp = source->now();
      message.header.frame_id = "device_frame";
      if (mode == "invalid") message.position = {std::numeric_limits<double>::quiet_NaN()};
      publisher->publish(message);
    }
    rcl_interfaces::msg::Log message;
    message.name = "position_publisher_node_7";
    message.level = 40;
    message.msg = "fixture observed warning; not a command or completion 한글";
    message.stamp = source->now();
    if (mode == "bad_log") message.stamp.nanosec = 1000000000;
    if (mode == "bad_level") message.level = 99;
    if (mode == "escaped_burst") {
      message.msg = std::string(512, '\\');
      message.file = message.function = std::string(128, '\\');
    }
    for (int j = 0; j < ((mode == "burst" || mode == "escaped_burst") ? 50 : 1); ++j)
      log->publish(message);
    auto facts = processes;
    if (mode == "escaped_burst") {
      while (facts.size() < 32)
        facts.push_back({1, 7, std::string(128, '\\'), std::string(128, '\\')});
    }
    observer->Tick(facts, 1, "monitoring");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  if (child > 0) {
    kill(child, SIGTERM);
    waitpid(child, nullptr, 0);
  }
  context->shutdown("fixture complete");
}
