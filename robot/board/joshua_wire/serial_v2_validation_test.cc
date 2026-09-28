// Hardware-free safety-gate and workflow tests for the manual serial-v2 tool.
// Exchanges run the actual AM243 software-only handler and firmware endpoint.
#include "robot/board/joshua_wire/serial_v2_validation.h"

#include <limits>
#include <sstream>
#include <vector>

#include "firmware/am243/joshua_dual_transport_v1/src/joshua_serial_commands.h"
#include "firmware/common/joshua_wire_serial_endpoint.h"
#include "gtest/gtest.h"

namespace robot::board::diagnostics {
namespace {
using Bytes = std::vector<uint8_t>;
class Firmware : public robot::comm::MessageTransport {
 public:
  Firmware() {
    jw_serial_endpoint_init(&endpoint, 2);
    state.latch_estop = true;
  }
  absl::Status Send(absl::Span<const uint8_t>) override {
    return absl::UnimplementedError("v2 only");
  }
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> bytes) override {
    jw2_frame_t request;
    if (jw2_decode_frame(bytes.data(), bytes.size(), &request) != 0)
      return absl::DataLossError("Not v2");
    commands.push_back(request.cmd);
    sessions.push_back(request.session_id);
    Bytes response(JW2_MAX_FRAME_LEN);
    const int len = jw_serial_endpoint_process(&endpoint,
                                               bytes.data(),
                                               bytes.size(),
                                               response.data(),
                                               response.size(),
                                               JoshuaSerialCommand,
                                               JoshuaSerialReset,
                                               &state);
    if (request.cmd == fail_cmd || len <= 0)
      return absl::DeadlineExceededError("lost reply after execution");
    response.resize(len);
    if (request.cmd == JW_CMD_IDENTIFY && wrong_identity) {
      jw2_frame_t reply;
      EXPECT_EQ(jw2_decode_frame(response.data(), response.size(), &reply), 0);
      Bytes payload(reply.payload, reply.payload + reply.payload_len);
      payload[0] = JW_BOARD_ESP32;
      response.resize(JW2_MAX_FRAME_LEN);
      response.resize(jw2_encode_response(
          response.data(), response.size(), &request, payload.data(), payload.size()));
    }
    return response;
  }
  jw_serial_endpoint_t endpoint{};
  JoshuaSerialChannel state{};
  std::vector<uint8_t> commands;
  std::vector<uint32_t> sessions;
  int fail_cmd = -1;
  bool wrong_identity = false;
};

class ValidationTest : public ::testing::Test {
 protected:
  Board board;
  SerialV2ValidationOptions options;
  std::shared_ptr<Firmware> firmware = std::make_shared<Firmware>();
  std::ostringstream output;
  void SetUp() override {
    board.set_name("test");
    board.set_board_type(AM243);
    board.set_protocol(JOSHUA_WIRE_V2);
    board.mutable_firmware()->set_min_proto_version(2);
    auto* comm = board.mutable_comm();
    comm->set_comm_type(robot::comm::SERIAL);
    comm->set_transport_type(robot::comm::MESSAGE);
    comm->mutable_serial_config()->set_port("never-opened");
    comm->mutable_serial_config()->set_baudrate(115200);
    auto* channel = board.add_channels();
    channel->set_index(0);
    channel->set_drive(STEP_DIR);
    auto* step = channel->mutable_step_dir();
    step->set_max_pulse_rate_hz(1000);
    step->set_step_pin(2);
    step->set_dir_pin(3);
    step->set_enable_pin(4);
  }
  absl::Status Run(std::function<bool()> cancelled = {}) {
    return RunSerialV2Validation(board, options, firmware, output, std::move(cancelled));
  }
  void Exercise() {
    options.mode = "exercise";
    options.channel = 0;
    options.allow_enable = true;
    options.target_steps = 10.0f;
  }
};

TEST_F(ValidationTest, DefaultNeverConfiguresEnablesOrTargets) {
  ASSERT_TRUE(Run().ok());
  EXPECT_EQ(firmware->commands, (Bytes{JW_CMD_RESET_SESSION, JW_CMD_IDENTIFY, JW_CMD_ESTOP}));
  EXPECT_FALSE(firmware->state.enabled);
  EXPECT_FALSE(firmware->state.configured);
}
TEST_F(ValidationTest, ConfigureStaysDisabled) {
  options.mode = "configure";
  options.channel = 0;
  ASSERT_TRUE(Run().ok());
  EXPECT_EQ(firmware->commands,
            (Bytes{JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_CONFIGURE_CHANNEL,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_ESTOP}));
  EXPECT_FALSE(firmware->state.enabled);
  EXPECT_TRUE(firmware->state.configured);
}
TEST_F(ValidationTest, ExerciseRequiresAllOptInsBeforeOpening) {
  options.mode = "exercise";
  options.channel = 0;
  EXPECT_FALSE(Run().ok());
  options.allow_enable = true;
  EXPECT_FALSE(Run().ok());
  options.target_steps = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(Run().ok());
  options.target_steps = std::numeric_limits<float>::max();
  EXPECT_FALSE(Run().ok());
  EXPECT_TRUE(firmware->commands.empty());
}
TEST_F(ValidationTest, ExerciseHoldsBeforeEnableAndStopsAfterTarget) {
  Exercise();
  ASSERT_TRUE(Run().ok());
  EXPECT_EQ(firmware->commands,
            (Bytes{JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_CONFIGURE_CHANNEL,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_SET_TARGET,
                   JW_CMD_ENABLE,
                   JW_CMD_SET_TARGET,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_DISABLE,
                   JW_CMD_ESTOP}));
  EXPECT_FALSE(firmware->state.enabled);
  EXPECT_TRUE(firmware->state.estopped);
  EXPECT_FLOAT_EQ(firmware->state.target_value, 10);
}
TEST_F(ValidationTest, TimeoutAfterEnableStillAttemptsEstop) {
  Exercise();
  firmware->fail_cmd = JW_CMD_ENABLE;
  EXPECT_EQ(Run().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(firmware->commands.back(), JW_CMD_ESTOP);
  EXPECT_FALSE(firmware->state.enabled);
}
TEST_F(ValidationTest, FailedCleanupReportsUnknownState) {
  firmware->fail_cmd = JW_CMD_ESTOP;
  const auto status = Run();
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("hardware state unknown"), std::string::npos);
}
TEST_F(ValidationTest, CancellationAfterEnableStillStops) {
  Exercise();
  EXPECT_EQ(Run([this] { return firmware->state.enabled; }).code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(firmware->commands.back(), JW_CMD_ESTOP);
  EXPECT_FALSE(firmware->state.enabled);
}
TEST_F(ValidationTest, IdentityMismatchPreventsConfiguration) {
  Exercise();
  firmware->wrong_identity = true;
  EXPECT_EQ(Run().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(firmware->commands, (Bytes{JW_CMD_RESET_SESSION, JW_CMD_IDENTIFY, JW_CMD_ESTOP}));
}
TEST_F(ValidationTest, RepeatedSessionsUseFreshIdsWithoutPhysicalReopen) {
  options.sessions = 2;
  ASSERT_TRUE(Run().ok());
  ASSERT_EQ(firmware->commands.size(), 6);
  EXPECT_NE(firmware->sessions[0], firmware->sessions[3]);
}
TEST_F(ValidationTest, BadConfigurationAndIrrelevantOptionsAreRejected) {
  options.allow_enable = true;
  EXPECT_FALSE(Run().ok());
  options.allow_enable = false;
  board.set_protocol(JOSHUA_WIRE_V1);
  EXPECT_FALSE(Run().ok());
  board.set_protocol(JOSHUA_WIRE_V2);
  board.mutable_comm()->set_comm_type(robot::comm::ETHERCAT);
  EXPECT_FALSE(Run().ok());
  board.mutable_comm()->set_comm_type(robot::comm::SERIAL);
  options.mode = "configure";
  options.channel = 0;
  board.mutable_channels(0)->mutable_step_dir()->set_enable_pin(2);
  EXPECT_FALSE(Run().ok());
  EXPECT_TRUE(firmware->commands.empty());
}
}  // namespace
}  // namespace robot::board::diagnostics
