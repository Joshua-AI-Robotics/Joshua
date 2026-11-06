#include "ros2/device_bus.h"

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <thread>

#include "config/validation.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "rclcpp/version.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/testing/fake_caller_sized_message_transport.h"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/float32.hpp"

namespace {
using namespace std::chrono_literals;

// Exercises the production board, motor and encoder factories; only the wire is fake.
class ServoWire : public robot::comm::FakeCallerSizedMessageTransport {
 public:
  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& bytes,
                                                      size_t size) override {
    ++reads;
    std::vector<uint8_t> params;
    if (size == 8)
      params = bytes[5] == 3 ? std::vector<uint8_t>{15, 3} : std::vector<uint8_t>{0, 8};
    std::vector<uint8_t> reply{255, 255, bytes[2], static_cast<uint8_t>(params.size() + 2), 0};
    reply.insert(reply.end(), params.begin(), params.end());
    unsigned sum = 0;
    for (size_t i = 2; i < reply.size(); ++i) sum += reply[i];
    reply.push_back(static_cast<uint8_t>(~sum));
    return reply;
  }
  int reads = 0;
};

class DeviceBusTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_NE(getenv("TEST_TMPDIR"), nullptr);
    ASSERT_EQ(setenv("ROS_LOG_DIR", getenv("TEST_TMPDIR"), 1), 0);
    ASSERT_EQ(setenv("ROS_DOMAIN_ID", RCLCPP_VERSION_MAJOR >= 20 ? "82" : "81", 1), 0);
    ASSERT_EQ(setenv("ROS_LOCALHOST_ONLY", "1", 1), 0);
    int argc = 0;
    rclcpp::init(argc, nullptr);
  }
  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }
  void SetUp() override {
    robot::board::BoardFactory::ResetForTesting();
    wire = std::make_shared<ServoWire>();
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(
        [this](const auto&) -> absl::StatusOr<robot::comm::CommTransport> {
          ++opens;
          return robot::comm::CommTransport{
              std::static_pointer_cast<robot::comm::MessageTransport>(wire)};
        });
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        R"pb(
          robot {
            boards {
              name: "servo_bus"
              board_type: FEETECH_BUS
              comm {
                comm_type: SERIAL
                transport_type: MESSAGE
                serial_config {
                  port: "/never-opened"
                  baudrate: 1000000
                  message_adapter: CALLER_SIZED
                }
              }
              channels {
                index: 1
                drive: SERVO_BUS_UART
                servo_bus { servo_id: 1 }
              }
            }
            actions {
              single_actions {
                action_type: ACTUATOR
                node {
                  id: 1
                  node_type: DEVICE_BUS
                  subscriptions { topic: "joint/position" ros2_data_type: FLOAT32 }
                  subscriptions { topic: "joint/torque" ros2_data_type: BOOL }
                }
                actuator {
                  actuator_name: "joint"
                  board_name: "servo_bus"
                  channel: 1
                  motor_type: MOTOR_STS3215
                  operational_lower_limit: 0
                  operational_upper_limit: 4095
                  sts3215_config { move_speed: 100 }
                }
              }
            }
            perceptions {
              single_perceptions {
                sensor_name: "joint"
                sensor_type: POSITION
                sts3215_encoder_config { board_name: "servo_bus" channel: 1 }
                node {
                  id: 1
                  node_type: DEVICE_BUS
                  publishers { topic: "measured" ros2_data_type: JOINT_STATE publish_rate_hz: 50 }
                }
              }
            }
          }
        )pb",
        &config));
    prefix = "test_bus_" + std::to_string(getpid());
    auto* a = config.mutable_robot()->mutable_actions()->mutable_single_actions(0);
    a->mutable_actuator()->set_actuator_name(prefix);
    for (auto& s : *a->mutable_node()->mutable_subscriptions())
      s.set_topic(prefix + s.topic().substr(s.topic().find('/')));
    config.mutable_robot()
        ->mutable_perceptions()
        ->mutable_single_perceptions(0)
        ->mutable_node()
        ->mutable_publishers(0)
        ->set_topic(prefix + "/measured");
  }
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
  }
  bool Await(const std::function<bool()>& condition) {
    auto deadline = std::chrono::steady_clock::now() + 4s;
    do {
      executor.spin_some();
      if (condition()) return true;
      std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
  }
  bool Wrote(uint8_t reg, uint8_t value) {
    for (const auto& w : wire->written_)
      if (w.size() > 6 && w[5] == reg && w[6] == value) return true;
    return false;
  }
  config::Config config;
  std::shared_ptr<ServoWire> wire;
  int opens = 0;
  std::string prefix;
  rclcpp::executors::SingleThreadedExecutor executor;
};

TEST_F(DeviceBusTest, CommandsAndNamedFeedbackShareOneBoardAndTransport) {
  ASSERT_TRUE(config::ValidateConfig(config).ok()) << config::ValidateConfig(config);
  auto runtime = std::make_shared<DeviceBus>(prefix, 1, config);
  EXPECT_EQ(opens, 1);
  EXPECT_TRUE(Wrote(40, 0));
  EXPECT_FALSE(Wrote(40, 1));
  auto client = std::make_shared<rclcpp::Node>(prefix + "_client");
  bool measured = false;
  auto feedback = client->create_subscription<sensor_msgs::msg::JointState>(
      prefix + "/measured", 1, [&](sensor_msgs::msg::JointState::ConstSharedPtr msg) {
        measured = msg->name == std::vector<std::string>{"joint"} && msg->position.size() == 1 &&
                   std::abs(msg->position[0] - M_PI) < 0.001;
      });
  auto enable = client->create_publisher<std_msgs::msg::Bool>(prefix + "/torque", 1);
  auto command = client->create_publisher<std_msgs::msg::Float32>(prefix + "/position", 1);
  executor.add_node(runtime);
  executor.add_node(client);
  ASSERT_TRUE(Await([&] {
    return measured && enable->get_subscription_count() == 1 &&
           command->get_subscription_count() == 1;
  }));
  std_msgs::msg::Bool on;
  on.data = true;
  enable->publish(on);
  ASSERT_TRUE(Await([&] { return Wrote(40, 1); }));
  std_msgs::msg::Float32 target;
  target.data = 2050;
  command->publish(target);
  ASSERT_TRUE(Await([&] { return Wrote(42, 2); }));
  EXPECT_EQ(opens, 1);
  wire->written_.clear();
  executor.remove_node(runtime);
  runtime.reset();
  EXPECT_TRUE(Wrote(40, 0));
  executor.remove_node(client);
}

TEST_F(DeviceBusTest, ValidationRejectsCompetingOwnerAndUnsupportedSensor) {
  config.mutable_robot()
      ->mutable_perceptions()
      ->mutable_single_perceptions(0)
      ->mutable_node()
      ->set_id(2);
  EXPECT_FALSE(config::ValidateConfig(config).ok());
  auto* s = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  s->mutable_node()->set_id(1);
  s->set_sensor_type(robot::perception::IMAGE);
  s->mutable_opencv_config()->set_id(0);
  EXPECT_FALSE(config::ValidateConfig(config).ok());
  EXPECT_EQ(opens, 0);
}
}  // namespace
