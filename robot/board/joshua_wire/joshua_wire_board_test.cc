#include "robot/board/joshua_wire/joshua_wire_board.h"

#include "firmware/common/joshua_wire.h"
#include "gtest/gtest.h"
#include "robot/board/joshua_wire/testing/fake_joshua_wire_transport.h"
#include "robot/comm/factory/comm_factory.h"
namespace robot::board {
namespace {
class FakeJoshuaWireBoard : public JoshuaWireBoard {
 public:
  FakeJoshuaWireBoard() : JoshuaWireBoard(ARDUINO_UNO) {}
};
void AddStepDirChannel(robot::board::Board* board,
                       uint32_t index,
                       uint32_t step_pin,
                       uint32_t dir_pin,
                       uint32_t enable_pin) {
  auto* channel = board->add_channels();
  channel->set_index(index);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  channel->mutable_step_dir()->set_max_pulse_rate_hz(20000);
  channel->mutable_step_dir()->set_invert_dir(false);
  channel->mutable_step_dir()->set_enable_active_low(true);
  channel->mutable_step_dir()->set_step_pin(step_pin);
  channel->mutable_step_dir()->set_dir_pin(dir_pin);
  channel->mutable_step_dir()->set_enable_pin(enable_pin);
}

robot::board::Board MakeBoardConfig() {
  robot::board::Board board;
  board.set_name("bridge_1");
  board.set_board_type(robot::board::BoardType::ARDUINO_UNO);
  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::SERIAL);
  comm->set_transport_type(robot::comm::TransportType::MESSAGE);
  comm->mutable_serial_config()->set_port("/dev/ttyACM0");
  comm->mutable_serial_config()->set_baudrate(115200);
  board.mutable_firmware()->set_min_proto_version(JW_PROTO_VERSION);

  AddStepDirChannel(&board, /*index=*/0, /*step_pin=*/2, /*dir_pin=*/3, /*enable_pin=*/4);
  return board;
}

class JoshuaWireBoardTest : public ::testing::Test {
 protected:
  std::shared_ptr<FakeJoshuaWireTransport> transport_ = std::make_shared<FakeJoshuaWireTransport>();
  void SetUp() override {
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(
        [this](const robot::comm::Comm&) -> absl::StatusOr<robot::comm::CommTransport> {
          return robot::comm::CommTransport{
              std::static_pointer_cast<robot::comm::MessageTransport>(transport_)};
        });
  }
  void TearDown() override {
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
  }
};
TEST_F(JoshuaWireBoardTest, DiscoversDescribesAndReconcilesBeforeConfiguringWithoutEnable) {
  FakeJoshuaWireBoard board;
  auto status = board.Init(MakeBoardConfig());
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_TRUE(transport_->channel.configured);
  EXPECT_FALSE(transport_->channel.enabled);
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  EXPECT_TRUE((*channel)->Enable().ok());
  EXPECT_TRUE((*channel)->SetTarget(TargetMode::kPosition, 42).ok());
  auto feedback = (*channel)->ReadFeedback();
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_EQ(feedback->position, 42);
  EXPECT_TRUE((*channel)->Disable().ok());
  EXPECT_TRUE(board.Teardown().ok());
  EXPECT_EQ((*channel)->Enable().code(), absl::StatusCode::kFailedPrecondition);
}
TEST_F(JoshuaWireBoardTest, VendorAndProductAreInformational) {
  transport_->channel.runtime.identity.vendor_id = 0xabcdef;
  transport_->channel.runtime.identity.product_id = 0x987654;
  FakeJoshuaWireBoard board;
  EXPECT_TRUE(board.Init(MakeBoardConfig()).ok());
}
TEST_F(JoshuaWireBoardTest, RequiredUnknownOrIncompatibleProfileBlocksConfiguration) {
  for (int scenario = 0; scenario < 3; ++scenario) {
    transport_->channel.function.descriptor.profile_id = scenario == 0 ? 999 : JW_PROFILE_ACTUATOR;
    transport_->channel.function.descriptor.profile_major = scenario == 1 ? 2 : 1;
    transport_->channel.function.descriptor.operations = scenario == 2 ? 0 : 31;
    FakeJoshuaWireBoard board;
    EXPECT_EQ(board.Init(MakeBoardConfig()).code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_FALSE(transport_->channel.configured);
  }
}
TEST_F(JoshuaWireBoardTest, HandlerBudgetAndRevisionAreCompatibilityGates) {
  transport_->channel.runtime.identity.max_handler_us = 200000;
  FakeJoshuaWireBoard board;
  EXPECT_FALSE(board.Init(MakeBoardConfig()).ok());
  transport_->channel.runtime.identity.max_handler_us = 100;
  transport_->channel.runtime.identity.protocol_min =
      transport_->channel.runtime.identity.protocol_max = 3;
  EXPECT_FALSE(board.Init(MakeBoardConfig()).ok());
}
TEST_F(JoshuaWireBoardTest, EstopLatchSurvivesResetAndBlocksReinitialization) {
  transport_->channel.estopped = true;
  transport_->channel.runtime.estopped = 1;
  FakeJoshuaWireBoard board;
  EXPECT_FALSE(board.Init(MakeBoardConfig()).ok());
  EXPECT_TRUE(transport_->channel.estopped);
  EXPECT_FALSE(transport_->channel.enabled);
}
TEST_F(JoshuaWireBoardTest, InvalidDesiredConfigNeverTouchesTransport) {
  for (int i = 0; i < 8; ++i) {
    auto config = MakeBoardConfig();
    switch (i) {
      case 0:
        config.set_protocol(static_cast<BoardProtocol>(1));
        break;
      case 1:
        config.set_board_type(MOCK);
        break;
      case 2:
        config.mutable_comm()->set_transport_type(robot::comm::BYTE_STREAM);
        break;
      case 3:
        config.clear_firmware();
        break;
      case 4:
        config.mutable_channels(0)->set_drive(PWM_DC);
        break;
      case 5:
        config.mutable_channels(0)->mutable_step_dir()->set_step_pin(300);
        break;
      case 6:
        config.mutable_channels(0)->mutable_step_dir()->set_step_pulse_width_us(70000);
        break;
      case 7:
        *config.add_channels() = config.channels(0);
        break;
    }
    FakeJoshuaWireBoard board;
    EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(transport_->exchange_calls_, 0);
  }
}
TEST_F(JoshuaWireBoardTest, FunctionIdsAreStableRatherThanArrayOffsets) {
  transport_->channel.function.descriptor.function_id = 200;
  auto config = MakeBoardConfig();
  config.mutable_channels(0)->set_index(200);
  FakeJoshuaWireBoard board;
  ASSERT_TRUE(board.Init(config).ok());
  EXPECT_TRUE(board.OpenChannel(200).ok());
  EXPECT_FALSE(board.OpenChannel(0).ok());
}
TEST_F(JoshuaWireBoardTest, UnsupportedModesAndStaleSamplesAreRejected) {
  FakeJoshuaWireBoard board;
  ASSERT_TRUE(board.Init(MakeBoardConfig()).ok());
  auto channel = *board.OpenChannel(0);
  const auto count = transport_->exchange_calls_;
  EXPECT_EQ(channel->SetTarget(TargetMode::kTorque, 1).code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport_->exchange_calls_, count);
  transport_->channel.feedback.metadata.validity = JW_SAMPLE_STALE;
  EXPECT_EQ(channel->ReadFeedback().status().code(), absl::StatusCode::kUnavailable);
}
}  // namespace
}  // namespace robot::board
