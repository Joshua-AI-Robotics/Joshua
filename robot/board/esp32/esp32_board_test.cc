#include "robot/board/esp32/esp32_board.h"

#include "gtest/gtest.h"
#include "robot/board/joshua_wire/testing/board_config.h"
#include "robot/board/joshua_wire/testing/fake_joshua_wire_transport.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::board {
namespace {
class Esp32BoardTest : public ::testing::Test {
 protected:
  std::shared_ptr<FakeJoshuaWireTransport> transport = std::make_shared<FakeJoshuaWireTransport>();
  void SetUp() override {
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(
        [this](const robot::comm::Comm&) -> absl::StatusOr<robot::comm::CommTransport> {
          return robot::comm::CommTransport{
              std::static_pointer_cast<robot::comm::MessageTransport>(transport)};
        });
  }
  void TearDown() override {
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
  }
};
TEST_F(Esp32BoardTest, CurrentEndpointDiscoversConfiguresAndControlsWithoutAutomaticEnable) {
  Esp32Board board;
  const auto status = board.Init(MakeStepDirBoardConfig(ESP32));
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_TRUE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.enabled);
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  EXPECT_TRUE((*channel)->Enable().ok());
  EXPECT_TRUE((*channel)->SetTarget(TargetMode::kPosition, 42).ok());
  auto feedback = (*channel)->ReadFeedback();
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_EQ(feedback->position, 42);
  EXPECT_TRUE(board.Teardown().ok());
  EXPECT_FALSE(transport->channel.enabled);
}
TEST_F(Esp32BoardTest, RejectsWrongBoardBindingBeforeOpeningTransport) {
  Esp32Board board;
  EXPECT_EQ(board.Init(MakeStepDirBoardConfig(MOCK)).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(transport->exchange_calls_, 0);
}
TEST_F(Esp32BoardTest, RejectsCallerSizedAdapterBeforeOpeningTransport) {
  auto config = MakeStepDirBoardConfig(ESP32);
  config.mutable_comm()->mutable_serial_config()->set_message_adapter(robot::comm::CALLER_SIZED);
  Esp32Board board;
  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(transport->exchange_calls_, 0);
}
TEST_F(Esp32BoardTest, RejectsRetiredWireRevisionWithoutConfiguringOutputs) {
  transport->channel.runtime.identity.protocol_min = 1;
  transport->channel.runtime.identity.protocol_max = 1;
  Esp32Board board;
  EXPECT_EQ(board.Init(MakeStepDirBoardConfig(ESP32)).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(transport->channel.configured);
}
}  // namespace
}  // namespace robot::board
