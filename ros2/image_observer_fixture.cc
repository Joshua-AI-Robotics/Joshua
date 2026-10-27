// Test-only synthetic publisher and launcher facts. No hardware dependencies.
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

#include "node_generator/runtime_observation.h"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  config::Config config;
  auto* camera = config.mutable_robot()->mutable_perceptions()->add_single_perceptions();
  camera->set_sensor_name("webcam");
  camera->set_sensor_type(robot::perception::IMAGE);
  auto* o = camera->mutable_opencv_config();
  o->set_width(1280);
  o->set_height(720);
  o->set_fps(30);
  camera->mutable_node()->set_id(12);
  camera->mutable_node()->set_node_type(ros2::node::CAMERA_PUBLISHER);
  auto* p = camera->mutable_node()->add_publishers();
  p->set_topic("/_joshua_image_fixture/image");
  p->set_ros2_data_type(ros2::data_type::IMAGE);
  p->set_publish_rate_hz(30);
  using Publisher = rclcpp::Publisher<sensor_msgs::msg::Image>;
  std::unique_ptr<node_generator::RuntimeObservation> observer;
  rclcpp::Context::SharedPtr context;
  rclcpp::Node::SharedPtr node, rogue;
  Publisher::SharedPtr publisher, other;
  auto create = [&]() {
    publisher.reset();
    other.reset();
    node.reset();
    rogue.reset();
    if (context) context->shutdown("fixture binding replacement");
    observer.reset();
    auto result = node_generator::RuntimeObservation::Prepare(
        config, "synthetic-fixture", argv[1], "", "webcam");
    if (!result.ok()) throw std::runtime_error(std::string(result.status().message()));
    observer = std::move(*result);
    context = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions init;
    init.auto_initialize_logging(false);
    init.shutdown_on_signal = false;
    context->init(0, nullptr, init);
    rclcpp::NodeOptions options;
    options.context(context).enable_rosout(false);
    node = std::make_shared<rclcpp::Node>("camera_publisher_node_12", options);
    rogue = std::make_shared<rclcpp::Node>("unrelated_camera", options);
    publisher = node->create_publisher<sensor_msgs::msg::Image>(
        p->topic(), rclcpp::QoS(1).best_effort().durability_volatile());
  };
  create();
  std::string mode = "normal";
  std::cout << "READY" << std::endl;
  for (int tick = 0; tick < 18000; ++tick) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN)) {
      std::getline(std::cin, mode);
      if (mode == "exit") break;
      if (mode == "rebind_topic") {
        p->set_topic("/joshua_image_fixture/rebound");
        create();
      }
      if (mode == "rebind_instance") create();
      if (mode == "rebind_domain") {
        auto d = std::stoi(std::getenv("ROS_DOMAIN_ID"));
        setenv("ROS_DOMAIN_ID", std::to_string((d + 1) % 232).c_str(), 1);
        create();
      }
      if (mode == "restart_silent") publisher.reset();
      other.reset();
      if (mode == "missing" || mode == "wrong")
        publisher.reset();
      else if (!publisher)
        publisher = node->create_publisher<sensor_msgs::msg::Image>(
            p->topic(), rclcpp::QoS(1).best_effort().durability_volatile());
      if (mode == "wrong" || mode == "ambiguous")
        other = rogue->create_publisher<sensor_msgs::msg::Image>(
            p->topic(), rclcpp::QoS(1).best_effort().durability_volatile());
      std::cout << "MODE " << mode << std::endl;
    }
    sensor_msgs::msg::Image m;
    m.width = 1280;
    m.height = 720;
    m.encoding = mode == "bgr" ? "bgr8" : "rgb8";
    m.step = m.width * 3 + (mode == "padded" ? 16 : 0);
    m.data.resize(m.step * m.height, 0);
    for (size_t y = 0; y < m.height; ++y)
      for (size_t x = 0; x < m.width; ++x)
        m.data[y * m.step + x * 3 + (mode == "bgr" ? 2 : 0)] = 255;
    m.header.stamp = node->now();
    m.header.frame_id = "synthetic_color_target:" + mode;
    if (mode == "invalid") m.step = 1;
    if (mode == "oversize") m.width = 1921;
    if (mode == "bad_stamp") m.header.stamp.nanosec = 1000000000;
    if (mode == "bad_frame_id") m.header.frame_id = std::string(257, 'x');
    if (mode == "bad_encoding") m.encoding = "rgba8";
    if (mode == "short") m.data.pop_back();
    if (publisher && mode != "silent" && mode != "restart_silent") publisher->publish(m);
    if (other) other->publish(m);
    if (mode != "pause_manifest")
      observer->Tick(
          {{int(getpid()), 12, "camera_publisher_node_12", "camera_publisher"}}, 1, "monitoring");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  context->shutdown("fixture done");
}
