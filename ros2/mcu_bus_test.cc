#include "ros2/mcu_bus.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "rclcpp/version.h"

namespace {
using namespace std::chrono_literals;

// Each acknowledgment is controlled separately so dispatch ordering can be
// checked without depending on serial timeouts or touching physical devices.
class StopBoard : public robot::board::BoardInterface {
 public:
  explicit StopBoard(absl::Status result)
      : result_(std::move(result)),
        entered_(enter_.get_future()),
        released_(release_.get_future()) {}
  absl::Status Init(const robot::board::Board&) override {
    return absl::OkStatus();
  }
  absl::StatusOr<std::shared_ptr<robot::board::BoardChannel>> OpenChannel(uint32_t) override {
    return absl::UnimplementedError("No channels in stop fixture");
  }
  absl::Status Teardown() override {
    return absl::OkStatus();
  }
  absl::Status EmergencyStop() override {
    enter_.set_value();
    released_.wait();
    return result_;
  }
  std::future_status WaitForStop(std::chrono::milliseconds timeout) {
    return entered_.wait_for(timeout);
  }
  void Release() {
    if (released_once_) return;
    release_.set_value();
    released_once_ = true;
  }

 private:
  absl::Status result_;
  std::promise<void> enter_, release_;
  std::future<void> entered_, released_;
  bool released_once_ = false;
};
}  // namespace

class McuBusTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(std::getenv("TEST_TMPDIR"), nullptr);
    ASSERT_EQ(setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1), 0);
    ASSERT_EQ(setenv("ROS_DOMAIN_ID", RCLCPP_VERSION_MAJOR >= 20 ? "86" : "85", 1), 0);
    ASSERT_EQ(setenv("ROS_LOCALHOST_ONLY", "1", 1), 0);
    rclcpp::init(0, nullptr);
    // Empty config avoids constructing any production board or serial owner.
    runtime_ = std::make_shared<McuBus>("stop_test", 1, config::Config{});
    client_node_ = std::make_shared<rclcpp::Node>("stop_test_client");
    client_ = client_node_->create_client<std_srvs::srv::Trigger>("/stop_test/estop");
    executor_ =
        std::make_unique<rclcpp::executors::MultiThreadedExecutor>(rclcpp::ExecutorOptions(), 2);
    executor_->add_node(runtime_);
    executor_->add_node(client_node_);
    spin_ = std::thread([this] { executor_->spin(); });
    ASSERT_TRUE(client_->wait_for_service(5s));
  }
  void TearDown() override {
    // Release even after an assertion failure so the service can finish.
    for (const auto& board : boards_) board->Release();
    if (executor_) executor_->cancel();
    if (spin_.joinable()) spin_.join();
    if (runtime_) executor_->remove_node(runtime_);
    if (client_node_) executor_->remove_node(client_node_);
    client_.reset();
    client_node_.reset();
    runtime_.reset();
    executor_.reset();
    rclcpp::shutdown();
  }
  std::shared_ptr<StopBoard> AddBoard(absl::Status result) {
    auto board = std::make_shared<StopBoard>(std::move(result));
    boards_.push_back(board);
    runtime_->boards_.push_back(board);
    return board;
  }

  std::vector<std::shared_ptr<StopBoard>> boards_;
  std::shared_ptr<McuBus> runtime_;
  rclcpp::Node::SharedPtr client_node_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr client_;
  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::thread spin_;
};

TEST_F(McuBusTest, DispatchesAllStopsBeforeWaitingAndCollectsEveryFailure) {
  auto first = AddBoard(absl::DeadlineExceededError("first board acknowledgment lost"));
  auto second = AddBoard(absl::UnavailableError("second board disconnected"));
  auto healthy = AddBoard(absl::OkStatus());
  healthy->Release();
  auto reply = client_->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());

  ASSERT_EQ(first->WaitForStop(2s), std::future_status::ready);
  // Neither blocked acknowledgment may prevent another board's stop dispatch.
  EXPECT_EQ(second->WaitForStop(2s), std::future_status::ready);
  EXPECT_EQ(healthy->WaitForStop(2s), std::future_status::ready);
  EXPECT_EQ(reply.wait_for(0s), std::future_status::timeout);
  first->Release();
  EXPECT_EQ(reply.wait_for(0s), std::future_status::timeout);
  second->Release();

  ASSERT_EQ(reply.wait_for(2s), std::future_status::ready);
  const auto response = reply.get();
  EXPECT_FALSE(response->success);
  EXPECT_NE(response->message.find("first board acknowledgment lost"), std::string::npos);
  EXPECT_NE(response->message.find("second board disconnected"), std::string::npos);
}

TEST_F(McuBusTest, ReportsSuccessWhenEveryBoardAcknowledges) {
  auto first = AddBoard(absl::OkStatus());
  auto second = AddBoard(absl::OkStatus());
  first->Release();
  second->Release();
  auto reply = client_->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
  ASSERT_EQ(reply.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(first->WaitForStop(0s), std::future_status::ready);
  EXPECT_EQ(second->WaitForStop(0s), std::future_status::ready);
  const auto response = reply.get();
  EXPECT_TRUE(response->success);
  EXPECT_EQ(response->message, "Protocol stop acknowledged; physical verification unsupported");
}
