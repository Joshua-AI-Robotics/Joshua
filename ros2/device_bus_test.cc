#include "ros2/device_bus.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
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
  absl::Status Write(const std::vector<uint8_t>& bytes) override {
    if (bytes.size() > 6 && bytes[5] == 40) enabled = bytes[6] != 0;
    return robot::comm::FakeCallerSizedMessageTransport::Write(bytes);
  }
  std::atomic<bool> enabled{false};
  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& bytes,
                                                      size_t size) override {
    ++reads;
    if (fail_reads) return absl::UnavailableError("Disconnected servo");
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
  std::atomic<int> reads{0};
  std::atomic<bool> fail_reads{false};
};

// A different physical board whose ESTOP acknowledgment is deliberately delayed.
class DelayedStopBoard : public robot::board::BoardInterface {
 public:
  absl::Status Init(const robot::board::Board&) override {
    return absl::OkStatus();
  }
  absl::Status Teardown() override {
    return absl::OkStatus();
  }
  absl::StatusOr<std::shared_ptr<robot::board::BoardChannel>> OpenChannel(uint32_t) override {
    return absl::UnimplementedError("No channels in delayed stop fixture");
  }
  absl::Status EmergencyStop() override {
    entered.set_value();
    released_.wait();
    return absl::DeadlineExceededError("Other board acknowledgment lost");
  }
  void Release() {
    if (!released_once_) {
      release_.set_value();
      released_once_ = true;
    }
  }
  std::promise<void> entered;

 private:
  std::promise<void> release_;
  std::shared_future<void> released_ = release_.get_future().share();
  bool released_once_ = false;
};
}  // namespace

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
  void AddDelayedBoard(DeviceBus& runtime, std::shared_ptr<DelayedStopBoard> board) {
    runtime.boards_.emplace("delayed_board", std::move(board));
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
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->mutable_observation_policy()->set_frame_id("base");
  auto* packet_output = sensor->mutable_node()->add_publishers();
  packet_output->set_topic(prefix + "/packet");
  packet_output->set_ros2_data_type(ros2::data_type::UINT8_MULTI_ARRAY);
  packet_output->set_observation_packet(true);
  packet_output->set_publish_rate_hz(50);
  ASSERT_TRUE(config::ValidateConfig(config).ok()) << config::ValidateConfig(config);
  auto runtime = std::make_shared<DeviceBus>(prefix, 1, config);
  EXPECT_EQ(opens, 1);
  EXPECT_TRUE(Wrote(40, 0));
  EXPECT_FALSE(Wrote(40, 1));
  auto client = std::make_shared<rclcpp::Node>(prefix + "_client");
  bool measured = false;
  int readiness = -1;
  std::set<int64_t> native_stamps, packet_stamps;
  bool atomic_metadata = false;
  auto packet_sub = client->create_subscription<std_msgs::msg::UInt8MultiArray>(
      prefix + "/packet", 1, [&](std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
        robot::perception::PerceptionPacket p;
        if (!p.ParseFromArray(msg->data.data(), msg->data.size())) return;
        const auto& m = p.sample_metadata();
        atomic_metadata = p.has_position() && m.measurement_valid() && !m.has_acquisition_ticks() &&
                          m.native_unit() == "sts3215_tick" && m.frame_id() == "base" &&
                          m.sequence() > 0 && !m.source_instance_id().empty();
        const auto stamp = ros2_utils::ToRosTimestamp(p.timestamp());
        if (stamp.ok()) packet_stamps.insert(rclcpp::Time(*stamp).nanoseconds());
      });
  auto status = client->create_subscription<std_msgs::msg::UInt8MultiArray>(
      prefix + "/measured/status", 1, [&](std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
        robot::perception::PerceptionPacket p;
        if (p.ParseFromArray(msg->data.data(), msg->data.size()) && p.has_observation_status())
          readiness = p.observation_status().state();
      });
  auto feedback = client->create_subscription<sensor_msgs::msg::JointState>(
      prefix + "/measured", 1, [&](sensor_msgs::msg::JointState::ConstSharedPtr msg) {
        native_stamps.insert(rclcpp::Time(msg->header.stamp).nanoseconds());
        measured = msg->header.frame_id == "base" &&
                   msg->name == std::vector<std::string>{"joint"} && msg->position.size() == 1 &&
                   std::abs(msg->position[0] - M_PI) < 0.001;
      });
  auto enable = client->create_publisher<std_msgs::msg::Bool>(prefix + "/torque", 1);
  auto command = client->create_publisher<std_msgs::msg::Float32>(prefix + "/position", 1);
  executor.add_node(runtime);
  executor.add_node(client);
  ASSERT_TRUE(Await([&] {
    bool matched = false;
    for (auto stamp : native_stamps) matched |= packet_stamps.count(stamp) != 0;
    return measured && atomic_metadata && matched &&
           readiness == robot::perception::ObservationStatus::READY &&
           enable->get_subscription_count() == 1 && command->get_subscription_count() == 1;
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
  wire->fail_reads = true;
  ASSERT_TRUE(Await([&] { return readiness == robot::perception::ObservationStatus::ERROR; }));
  wire->fail_reads = false;
  ASSERT_TRUE(Await([&] { return readiness == robot::perception::ObservationStatus::READY; }));
  wire->written_.clear();
  executor.remove_node(runtime);
  runtime.reset();
  EXPECT_TRUE(Wrote(40, 0));
  executor.remove_node(client);
}

TEST_F(DeviceBusTest, EstopFallsBackToChannelDisableAndRequiresExplicitRecovery) {
  auto runtime = std::make_shared<DeviceBus>(prefix, 1, config);
  auto client = std::make_shared<rclcpp::Node>(prefix + "_client");
  auto enable =
      client->create_client<std_srvs::srv::Trigger>("/" + prefix + "/" + prefix + "/enable");
  auto recover =
      client->create_client<std_srvs::srv::Trigger>("/" + prefix + "/" + prefix + "/recover");
  auto stop = client->create_client<std_srvs::srv::Trigger>("/" + prefix + "/estop");
  executor.add_node(runtime);
  executor.add_node(client);
  ASSERT_TRUE(Await([&] {
    return enable->service_is_ready() && recover->service_is_ready() && stop->service_is_ready();
  }));
  auto call = [&](const auto& service) {
    auto result = service->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    if (!Await([&] { return result.wait_for(0s) == std::future_status::ready; })) {
      ADD_FAILURE() << "Control service did not complete";
      return false;
    }
    return result.get()->success;
  };
  ASSERT_TRUE(call(enable));
  EXPECT_TRUE(Wrote(40, 1));
  wire->written_.clear();
  EXPECT_TRUE(call(stop));
  EXPECT_TRUE(Wrote(40, 0));
  EXPECT_FALSE(call(enable));
  EXPECT_FALSE(Wrote(40, 1));
  ASSERT_TRUE(call(recover));
  EXPECT_TRUE(call(enable));
  EXPECT_TRUE(Wrote(40, 1));
  executor.remove_node(runtime);
  runtime.reset();
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

TEST_F(DeviceBusTest, IndependentFallbackStopsBeforeSlowBoardAndKeepsBusRecoveryInterlocked) {
  config.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->mutable_control_policy()
      ->set_command_timeout_ms(10000);
  auto runtime = std::make_shared<DeviceBus>(prefix, 1, config);
  auto delayed = std::make_shared<DelayedStopBoard>();
  AddDelayedBoard(*runtime, delayed);
  auto entered = delayed->entered.get_future();
  auto client = std::make_shared<rclcpp::Node>(prefix + "_client");
  const auto actuator = "/" + prefix + "/" + prefix + "/";
  auto enable = client->create_client<std_srvs::srv::Trigger>(actuator + "enable");
  auto local_stop = client->create_client<std_srvs::srv::Trigger>(actuator + "stop");
  auto recover = client->create_client<std_srvs::srv::Trigger>(actuator + "recover");
  auto estop = client->create_client<std_srvs::srv::Trigger>("/" + prefix + "/estop");
  rclcpp::executors::MultiThreadedExecutor multi(rclcpp::ExecutorOptions(), 2);
  multi.add_node(runtime);
  multi.add_node(client);
  std::thread spin([&] { multi.spin(); });
  joshua::ScopeExit cleanup([&] {
    delayed->Release();
    multi.cancel();
    spin.join();
    multi.remove_node(runtime);
    multi.remove_node(client);
  });
  ASSERT_TRUE(enable->wait_for_service(2s));
  ASSERT_TRUE(local_stop->wait_for_service(2s));
  ASSERT_TRUE(recover->wait_for_service(2s));
  ASSERT_TRUE(estop->wait_for_service(2s));
  auto call = [&](const auto& service) {
    auto reply = service->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    if (reply.wait_for(2s) != std::future_status::ready) {
      ADD_FAILURE() << "Control service did not complete";
      return false;
    }
    return reply.get()->success;
  };
  ASSERT_TRUE(call(enable));
  ASSERT_TRUE(wire->enabled);
  auto stopped = estop->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
  ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);
  // The healthy Feetech board has no EmergencyStop implementation. Its actual
  // torque-disable fallback must complete while the other board still waits.
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (wire->enabled && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  EXPECT_FALSE(wire->enabled);
  EXPECT_EQ(stopped.wait_for(0s), std::future_status::timeout);
  EXPECT_TRUE(call(local_stop));
  EXPECT_FALSE(call(recover));
  EXPECT_FALSE(call(enable));
  EXPECT_FALSE(wire->enabled);

  delayed->Release();
  ASSERT_EQ(stopped.wait_for(2s), std::future_status::ready);
  auto response = stopped.get();
  EXPECT_FALSE(response->success);
  EXPECT_NE(response->message.find("Other board acknowledgment lost"), std::string::npos);
  EXPECT_FALSE(call(enable));
  ASSERT_TRUE(call(recover));
  EXPECT_TRUE(call(enable));
  EXPECT_TRUE(wire->enabled);
}
