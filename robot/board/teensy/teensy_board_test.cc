#include "robot/board/teensy/teensy_board.h"

#include <memory>

#include "absl/status/status.h"
#include "firmware/common/joshua_wire_v1.h"
#include "gtest/gtest.h"
#include "robot/board/frame/fake_frame_transport.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {
namespace {

std::vector<uint8_t> MakeIdentifyResponse(uint8_t n_channels) {
  jw1_identify_response_t response{};
  response.board_id = JW1_BOARD_TEENSY41;
  response.n_channels = n_channels;
  for (uint8_t i = 0; i < n_channels; i++) {
    response.channel_drives[i] = JW1_DRIVE_STEP_DIR;
  }
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_identify_response(buf, sizeof(buf), &response);
  return std::vector<uint8_t>(buf, buf + len);
}

std::vector<uint8_t> MakeStatusResponse(uint8_t cmd, uint8_t channel, jw1_status_t status) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_status_response(buf, sizeof(buf), cmd, channel, status);
  return std::vector<uint8_t>(buf, buf + len);
}

std::vector<uint8_t> MakeFeedbackResponse(uint8_t channel,
                                          float position,
                                          float velocity,
                                          uint16_t fault_flags) {
  jw1_feedback_t feedback{position, velocity, fault_flags};
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_feedback_response(buf, sizeof(buf), channel, &feedback);
  return std::vector<uint8_t>(buf, buf + len);
}

robot::board::Board MakeTeensyBoard() {
  robot::board::Board board;
  board.set_name("stepper_bus");
  board.set_board_type(robot::board::BoardType::TEENSY41);
  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::SERIAL);
  comm->mutable_serial_config()->set_port("/dev/ttyACM0");
  comm->mutable_serial_config()->set_baudrate(115200);
  board.mutable_firmware()->set_min_proto_version(1);

  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  channel->mutable_step_dir()->set_max_pulse_rate_hz(20000);
  channel->mutable_step_dir()->set_invert_dir(false);
  channel->mutable_step_dir()->set_enable_active_low(true);
  channel->mutable_step_dir()->set_step_pin(2);
  channel->mutable_step_dir()->set_dir_pin(3);
  channel->mutable_step_dir()->set_enable_pin(4);
  return board;
}

class TeensyBoardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    transport_ = std::make_shared<FakeFrameTransport>();
    TeensyBoard::SetFrameTransportFactoryForTesting(
        [this](const robot::comm::Comm&) -> absl::StatusOr<std::shared_ptr<FrameTransport>> {
          return transport_;
        });
  }

  void TearDown() override {
    TeensyBoard::SetFrameTransportFactoryForTesting(nullptr);
  }

  // Init() always does IDENTIFY then one CONFIGURE_CHANNEL per configured
  // channel; tests that expect Init to succeed must queue both first.
  void QueueSuccessfulInit(uint8_t n_channels = 1) {
    transport_->QueueResponse(MakeIdentifyResponse(n_channels));
    transport_->QueueResponse(MakeStatusResponse(JW1_CMD_CONFIGURE_CHANNEL, 0, JW1_STATUS_OK));
  }

  std::shared_ptr<FakeFrameTransport> transport_;
};

TEST_F(TeensyBoardTest, InitIdentifiesAndConfiguresEveryChannel) {
  QueueSuccessfulInit();
  TeensyBoard board;

  auto status = board.Init(MakeTeensyBoard());

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->send_calls_, 2);
}

TEST_F(TeensyBoardTest, InitRejectsWrongBoardType) {
  auto config = MakeTeensyBoard();
  config.set_board_type(robot::board::BoardType::MOCK);
  TeensyBoard board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TeensyBoardTest, InitRejectsMissingFirmwareSpec) {
  auto config = MakeTeensyBoard();
  config.clear_firmware();
  TeensyBoard board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TeensyBoardTest, InitRejectsNonStepDirDrive) {
  auto config = MakeTeensyBoard();
  config.mutable_channels(0)->set_drive(robot::board::DriveInterface::SERVO_BUS_UART);
  TeensyBoard board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TeensyBoardTest, InitRejectsPinOutOfMcuRange) {
  auto config = MakeTeensyBoard();
  config.mutable_channels(0)->mutable_step_dir()->set_step_pin(300);  // Doesn't fit uint8_t.
  TeensyBoard board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TeensyBoardTest, InitRejectsDuplicateChannelIndex) {
  auto config = MakeTeensyBoard();
  auto* channel = config.add_channels();
  channel->set_index(0);  // Same index as the existing channel.
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  TeensyBoard board;

  EXPECT_EQ(board.Init(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TeensyBoardTest, InitFailsWhenFirmwareReportsFewerChannels) {
  transport_->QueueResponse(MakeIdentifyResponse(/*n_channels=*/0));
  TeensyBoard board;

  EXPECT_EQ(board.Init(MakeTeensyBoard()).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(TeensyBoardTest, InitFailsWhenConfigureChannelReturnsError) {
  transport_->QueueResponse(MakeIdentifyResponse(1));
  transport_->QueueResponse(MakeStatusResponse(JW1_CMD_CONFIGURE_CHANNEL, 0, JW1_STATUS_ERROR));
  TeensyBoard board;

  EXPECT_EQ(board.Init(MakeTeensyBoard()).code(), absl::StatusCode::kInternal);
}

TEST_F(TeensyBoardTest, OpenChannelEnableSendsEnableFrame) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok()) << channel.status();

  transport_->QueueResponse(MakeStatusResponse(JW1_CMD_ENABLE, 0, JW1_STATUS_OK));
  auto status = (*channel)->Enable();

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->last_sent_[3], JW1_CMD_ENABLE);
  EXPECT_EQ(transport_->last_sent_[4], 0);
}

TEST_F(TeensyBoardTest, SetTargetPositionSendsSetTargetFrame) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok());

  transport_->QueueResponse(MakeStatusResponse(JW1_CMD_SET_TARGET, 0, JW1_STATUS_OK));
  auto status = (*channel)->SetTarget(TargetMode::kPosition, 1234.0f);

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport_->last_sent_[3], JW1_CMD_SET_TARGET);
  EXPECT_EQ(transport_->last_sent_[5], JW1_MODE_POSITION);
}

TEST_F(TeensyBoardTest, SetTargetTorqueIsUnimplementedWithoutTouchingWire) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  const int calls_before = transport_->send_calls_;

  EXPECT_EQ((*channel)->SetTarget(TargetMode::kTorque, 1.0f).code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport_->send_calls_, calls_before);
}

TEST_F(TeensyBoardTest, ReadFeedbackDecodesPositionAndVelocity) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());
  auto channel = board.OpenChannel(0);
  ASSERT_TRUE(channel.ok());

  transport_->QueueResponse(MakeFeedbackResponse(0, 4200.0f, -15.5f, 0));
  auto feedback = (*channel)->ReadFeedback();

  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_FLOAT_EQ(feedback->position, 4200.0f);
  EXPECT_FLOAT_EQ(feedback->velocity, -15.5f);
  EXPECT_EQ(feedback->fault_flags, 0);
}

TEST_F(TeensyBoardTest, OpenChannelOutOfRangeIsNotFound) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());

  auto channel = board.OpenChannel(5);

  EXPECT_EQ(channel.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(TeensyBoardTest, TeardownDropsChannels) {
  QueueSuccessfulInit();
  TeensyBoard board;
  ASSERT_TRUE(board.Init(MakeTeensyBoard()).ok());

  ASSERT_TRUE(board.Teardown().ok());

  EXPECT_EQ(board.OpenChannel(0).status().code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace robot::board
