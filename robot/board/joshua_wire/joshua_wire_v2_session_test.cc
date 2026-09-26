// Hardware-free host session and production JoshuaWireBoard integration tests.
// A loopback transport runs the real AM243 software command handler and shared
// firmware endpoint to test correlation, failure/reconnect, ID exhaustion,
// concurrent callers, ESTOP and teardown without opening a physical device.
#include "robot/board/joshua_wire/joshua_wire_v2_session.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "firmware/am243/joshua_dual_transport_v1/src/joshua_serial_commands.h"
#include "firmware/common/joshua_wire_serial_endpoint.h"
#include "firmware/common/joshua_wire_v1.h"
#include "firmware/common/joshua_wire_v2.h"
#include "gtest/gtest.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::board {
namespace {
using Bytes = std::vector<uint8_t>;

Bytes Command(uint8_t cmd, uint8_t channel = 0) {
  uint8_t buffer[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_frame(buffer, sizeof(buffer), cmd, channel, nullptr, 0);
  return Bytes(buffer, buffer + len);
}

Bytes Configure() {
  uint8_t buffer[JW1_MAX_FRAME_LEN];
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 1000;
  const int len = jw1_encode_configure_channel_step_dir(buffer, sizeof(buffer), 0, &config);
  return Bytes(buffer, buffer + len);
}

// Runs the real AM243 serial command implementation and shared firmware session
// endpoint. No NIC, device node, ROS graph, or TI SDK participates in this test.
class FirmwareTransport : public robot::comm::MessageTransport {
 public:
  FirmwareTransport() {
    jw_serial_endpoint_init(&endpoint, 2);
    channel.latch_estop = true;
  }
  absl::Status Open() override {
    return absl::OkStatus();
  }
  absl::Status Write(const Bytes&) override {
    return absl::UnimplementedError("No send-only exchange in v2.");
  }
  absl::StatusOr<Bytes> SendAndReceive(const Bytes&, size_t) override {
    return absl::UnimplementedError("V2 must use framed Exchange.");
  }
  absl::StatusOr<Bytes> Exchange(const Bytes& request) override {
    EXPECT_EQ(active.fetch_add(1), 0);
    if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    requests.push_back(request);
    Bytes response(JW2_MAX_FRAME_LEN);
    const int len = jw_serial_endpoint_process(&endpoint,
                                               request.data(),
                                               request.size(),
                                               response.data(),
                                               response.size(),
                                               JoshuaSerialCommand,
                                               JoshuaSerialReset,
                                               &channel);
    --active;
    if (timeout || len == 0) return absl::DeadlineExceededError("Response lost");
    if (len < 0) return absl::InternalError("Firmware handler failed");
    response.resize(len);
    if (!retained.empty()) return retained;
    if (corrupt_field >= 0) {
      jw2_frame_t frame;
      EXPECT_EQ(jw2_decode_frame(response.data(), response.size(), &frame), 0);
      const Bytes payload(frame.payload, frame.payload + frame.payload_len);
      switch (corrupt_field) {
        case 0:
          ++frame.session_id;
          break;
        case 1:
          ++frame.message_id;
          break;
        case 2:
          ++frame.cmd;
          break;
        case 3:
          ++frame.channel;
          break;
      }
      Bytes altered(JW2_MAX_FRAME_LEN);
      const int size = jw2_encode_frame(altered.data(),
                                        altered.size(),
                                        frame.session_id,
                                        frame.message_id,
                                        frame.cmd,
                                        frame.channel,
                                        payload.data(),
                                        payload.size());
      altered.resize(size);
      return altered;
    }
    last_response = response;
    return response;
  }
  jw_serial_endpoint_t endpoint{};
  JoshuaSerialChannel channel{};
  std::vector<Bytes> requests;
  Bytes retained;
  Bytes last_response;
  bool timeout = false;
  bool delay = false;
  int corrupt_field = -1;
  std::atomic<int> active{0};
};

class V2SessionTest : public ::testing::Test {
 protected:
  std::shared_ptr<FirmwareTransport> transport = std::make_shared<FirmwareTransport>();
  uint32_t session_id = 10;
  JoshuaWireV2Session session{transport, [this] { return ++session_id; }};

  absl::StatusOr<Bytes> Exchange(const Bytes& request) {
    return session.SendAndReceive(request, 0);
  }
};

TEST_F(V2SessionTest, HandshakeGatesAllCommandsAndPropagatesErrors) {
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(transport->requests.empty());
  ASSERT_TRUE(session.Open().ok());
  auto enable = Exchange(Command(JW_CMD_ENABLE));
  ASSERT_TRUE(enable.ok());
  EXPECT_EQ((*enable)[5], JW_STATUS_ERROR);  // Firmware has not been configured.
  ASSERT_TRUE(Exchange(Configure()).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ENABLE)).ok());
  EXPECT_TRUE(transport->channel.enabled);
  auto identify = Exchange(Command(JW_CMD_IDENTIFY, JW_CHANNEL_NONE));
  ASSERT_TRUE(identify.ok());
  EXPECT_EQ(identify->size(), JW1_FRAME_LEN(JW_IDENTIFY_RESPONSE_PAYLOAD_LEN));
  EXPECT_EQ(transport->requests.front()[11], JW_CMD_RESET_SESSION);
}

TEST_F(V2SessionTest, EveryCorrelationFieldMustMatch) {
  ASSERT_TRUE(session.Open().ok());
  for (int field = 0; field < 4; ++field) {
    transport->corrupt_field = field;
    EXPECT_EQ(Exchange(Command(JW_CMD_GET_FEEDBACK)).status().code(), absl::StatusCode::kDataLoss);
  }
}

TEST_F(V2SessionTest, TimeoutAndLateResponseNeverReuseAnId) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(Exchange(Configure()).ok());
  transport->timeout = true;
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(transport->channel.enabled);  // Request executed; response was lost.
  jw2_frame_t timed_out;
  const auto request = transport->requests.back();
  ASSERT_EQ(jw2_decode_frame(request.data(), request.size(), &timed_out), 0);
  transport->timeout = false;
  transport->retained.resize(JW2_MAX_FRAME_LEN);
  const uint8_t ok = 0;
  const int len = jw2_encode_response(
      transport->retained.data(), transport->retained.size(), &timed_out, &ok, 1);
  transport->retained.resize(len);
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDataLoss);
  jw2_frame_t next;
  ASSERT_EQ(
      jw2_decode_frame(transport->requests.back().data(), transport->requests.back().size(), &next),
      0);
  EXPECT_GT(next.message_id, timed_out.message_id);
  transport->retained.clear();
  EXPECT_TRUE(Exchange(Command(JW_CMD_DISABLE)).ok());
}

TEST_F(V2SessionTest, ReconnectAndFirmwareRebootRequireFreshReset) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(Exchange(Configure()).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ENABLE)).ok());
  const auto old_response = transport->last_response;
  ASSERT_TRUE(session.Open().ok());
  EXPECT_FALSE(transport->channel.enabled);
  EXPECT_FALSE(transport->channel.configured);
  transport->retained = old_response;
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDataLoss);
  transport->retained.clear();
  jw_serial_endpoint_init(&transport->endpoint, 2);
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDeadlineExceeded);
  ASSERT_TRUE(session.Open().ok());
  EXPECT_TRUE(Exchange(Configure()).ok());
}

TEST_F(V2SessionTest, FailedResetAndInvalidIdSourceKeepSessionClosed) {
  transport->timeout = true;
  EXPECT_EQ(session.Open().code(), absl::StatusCode::kDeadlineExceeded);
  const auto count = transport->requests.size();
  EXPECT_EQ(Exchange(Configure()).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
  JoshuaWireV2Session zero(transport, [] { return 0; });
  EXPECT_EQ(zero.Open().code(), absl::StatusCode::kUnavailable);
  transport->timeout = false;
  JoshuaWireV2Session repeated(transport, [] { return 100; });
  EXPECT_TRUE(repeated.Open().ok());
  EXPECT_EQ(repeated.Open().code(), absl::StatusCode::kUnavailable);
}

TEST_F(V2SessionTest, ExhaustionStopsResetsAndRestoresOnlyConfiguration) {
  JoshuaWireV2Session small(transport, [this] { return ++session_id; }, 5);
  ASSERT_TRUE(small.Open().ok());                                           // ID 1
  ASSERT_TRUE(small.SendAndReceive(Configure(), 0).ok());                   // ID 2
  ASSERT_TRUE(small.SendAndReceive(Command(JW_CMD_ENABLE), 0).ok());        // ID 3
  ASSERT_TRUE(small.SendAndReceive(Command(JW_CMD_GET_FEEDBACK), 0).ok());  // ID 4
  auto rotated = small.SendAndReceive(Command(JW_CMD_GET_FEEDBACK), 0);
  EXPECT_EQ(rotated.status().code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_EQ(transport->requests.size(), 7);
  EXPECT_EQ(transport->requests[4][11], JW_CMD_ESTOP);
  EXPECT_EQ(transport->requests[5][11], JW_CMD_RESET_SESSION);
  EXPECT_EQ(transport->requests[6][11], JW_CMD_CONFIGURE_CHANNEL);
  EXPECT_TRUE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.enabled);
  EXPECT_FALSE(transport->channel.estopped);
  EXPECT_TRUE(small.SendAndReceive(Command(JW_CMD_ENABLE), 0).ok());
  EXPECT_TRUE(transport->channel.enabled);
}

TEST_F(V2SessionTest, FailedStopAtExhaustionDoesNotResetOrReuseLastId) {
  JoshuaWireV2Session small(transport, [this] { return ++session_id; }, 3);
  ASSERT_TRUE(small.Open().ok());
  ASSERT_TRUE(small.SendAndReceive(Configure(), 0).ok());
  transport->timeout = true;
  EXPECT_EQ(small.SendAndReceive(Command(JW_CMD_ENABLE), 0).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  const auto count = transport->requests.size();
  EXPECT_EQ(small.SendAndReceive(Command(JW_CMD_ENABLE), 0).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
}

TEST_F(V2SessionTest, ConcurrentCallersSerializeAcrossChannels) {
  ASSERT_TRUE(session.Open().ok());
  transport->delay = true;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([this] {
      for (int j = 0; j < 10; ++j) EXPECT_TRUE(Exchange(Command(JW_CMD_GET_FEEDBACK)).ok());
    });
  }
  for (auto& thread : threads) thread.join();
  uint32_t expected = 1;
  for (const auto& bytes : transport->requests) {
    jw2_frame_t frame;
    ASSERT_EQ(jw2_decode_frame(bytes.data(), bytes.size(), &frame), 0);
    EXPECT_EQ(frame.message_id, expected++);
  }
}

TEST_F(V2SessionTest, EstopStaysLatchedUntilResetAndReconfiguration) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(Exchange(Configure()).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ENABLE)).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ESTOP, JW_CHANNEL_NONE)).ok());
  EXPECT_TRUE(transport->channel.estopped);
  EXPECT_FALSE(transport->channel.enabled);
  auto result = Exchange(Command(JW_CMD_ENABLE));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ((*result)[5], JW_STATUS_ERROR);
  ASSERT_TRUE(session.Open().ok());
  EXPECT_FALSE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.estopped);
}

TEST_F(V2SessionTest, ProductionBoardInitializesAndUsesAllSerialChannelCommands) {
  robot::comm::CommFactory::SetCommTransportFactoryForTesting(
      [this](const robot::comm::Comm&) -> absl::StatusOr<robot::comm::CommTransport> {
        return robot::comm::CommTransport{
            std::static_pointer_cast<robot::comm::MessageTransport>(transport)};
      });
  struct ResetFactory {
    ~ResetFactory() {
      robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
    }
  } reset_factory;
  Board config;
  config.set_name("serial_v2");
  config.set_board_type(AM243);
  config.set_protocol(JOSHUA_WIRE_V2);
  config.mutable_firmware()->set_min_proto_version(2);
  auto* comm = config.mutable_comm();
  comm->set_comm_type(robot::comm::SERIAL);
  comm->set_transport_type(robot::comm::MESSAGE);
  comm->mutable_serial_config()->set_port("not-opened");
  comm->mutable_serial_config()->set_baudrate(115200);
  auto* channel_config = config.add_channels();
  channel_config->set_index(0);
  channel_config->set_drive(STEP_DIR);
  channel_config->mutable_step_dir()->set_max_pulse_rate_hz(1000);
  JoshuaWireBoard board(AM243, JW_BOARD_AM243);
  ASSERT_TRUE(board.Init(config).ok());
  ASSERT_EQ(transport->requests.size(), 3);  // RESET, IDENTIFY, CONFIGURE
  auto opened = board.OpenChannel(0);
  ASSERT_TRUE(opened.ok());
  auto channel = *opened;
  EXPECT_TRUE(channel->Enable().ok());
  EXPECT_TRUE(channel->SetTarget(TargetMode::kPosition, 123).ok());
  auto feedback = channel->ReadFeedback();
  ASSERT_TRUE(feedback.ok());
  EXPECT_FLOAT_EQ(feedback->position, 123);
  EXPECT_TRUE(channel->Disable().ok());
  EXPECT_FALSE(transport->channel.enabled);
  EXPECT_TRUE(board.Teardown().ok());
  EXPECT_TRUE(transport->channel.estopped);
  const auto count = transport->requests.size();
  EXPECT_EQ(channel->Enable().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
}
}  // namespace
}  // namespace robot::board
