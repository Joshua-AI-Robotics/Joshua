// Hardware-free host session and production JoshuaWireBoard integration tests.
// A loopback transport runs the real AM243 software command handler and shared
// firmware endpoint to test correlation, failure/reconnect, ID exhaustion,
// concurrent callers, ESTOP and teardown without opening a physical device.
#include "robot/board/joshua_wire/joshua_wire_session.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "firmware/am243/joshua_dual_transport/src/joshua_commands.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_endpoint.h"
#include "gtest/gtest.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::board {
namespace {
using Bytes = std::vector<uint8_t>;

jw_command_t Command(uint8_t cmd, uint8_t channel = 0) {
  return {cmd, channel, nullptr, 0};
}

jw_command_t Configure() {
  static const auto payload = [] {
    std::array<uint8_t, JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN> bytes{};
    jw_configure_step_dir_t config{};
    config.max_pulse_rate_hz = 1000;
    EXPECT_EQ(jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config),
              bytes.size());
    return bytes;
  }();
  return {JW_CMD_CONFIGURE_CHANNEL, 0, payload.data(), payload.size()};
}

// Runs the real AM243 serial command implementation and shared firmware session
// endpoint. No NIC, device node, ROS graph, or TI SDK participates in this test.
class FirmwareTransport : public robot::comm::MessageTransport {
 public:
  FirmwareTransport() {
    jw_endpoint_init(&endpoint);
  }
  absl::Status Send(absl::Span<const uint8_t>) override {
    return absl::UnimplementedError("No send-only exchange in JW.");
  }
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request) override {
    EXPECT_EQ(active.fetch_add(1), 0);
    if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    requests.emplace_back(request.begin(), request.end());
    Bytes response(JW_MAX_FRAME_LEN);
    const int len = jw_endpoint_process(&endpoint,
                                        request.data(),
                                        request.size(),
                                        response.data(),
                                        response.size(),
                                        handler,
                                        JoshuaReset,
                                        &channel);
    --active;
    if (timeout || len == 0) return absl::DeadlineExceededError("Response lost");
    if (len < 0) return absl::InternalError("Firmware handler failed");
    response.resize(len);
    if (!retained.empty()) return retained;
    if (corrupt_field >= 0) {
      jw_frame_t frame;
      EXPECT_EQ(jw_decode_frame(response.data(), response.size(), &frame), 0);
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
      Bytes altered(JW_MAX_FRAME_LEN);
      const int size = jw_encode_frame(altered.data(),
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
  jw_endpoint_t endpoint{};
  jw_command_handler_fn handler = JoshuaCommand;
  JoshuaChannel channel{};
  std::vector<Bytes> requests;
  Bytes retained;
  Bytes last_response;
  bool timeout = false;
  bool delay = false;
  int corrupt_field = -1;
  std::atomic<int> active{0};
};

class JoshuaWireSessionTest : public ::testing::Test {
 protected:
  std::shared_ptr<FirmwareTransport> transport = std::make_shared<FirmwareTransport>();
  uint32_t session_id = 10;
  JoshuaWireSession session{transport, [this] { return ++session_id; }};

  absl::StatusOr<Bytes> Exchange(const jw_command_t& request) {
    return session.Exchange(request);
  }
};

class RoutedCyclic : public robot::comm::CorrelatedCyclicTransport {
 public:
  explicit RoutedCyclic(std::shared_ptr<FirmwareTransport> firmware)
      : firmware(std::move(firmware)) {}
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request,
                                 absl::Duration timeout) override {
    jw_frame_t frame;
    EXPECT_EQ(jw_decode_frame(request.data(), request.size(), &frame), 0);
    EXPECT_TRUE(frame.cmd == JW_CMD_SET_TARGET || frame.cmd == JW_CMD_GET_FEEDBACK);
    EXPECT_EQ(timeout, absl::Milliseconds(37));
    commands.push_back(frame.cmd);
    return firmware->Exchange(request);
  }
  std::shared_ptr<FirmwareTransport> firmware;
  std::vector<uint8_t> commands;
};

TEST_F(JoshuaWireSessionTest, PairedRoutingSharesIdsAndPreservesCorrelationAndTimeoutPolicy) {
  auto cyclic = std::make_shared<RoutedCyclic>(transport);
  JoshuaWireSession routed(
      transport, [] { return 99; }, UINT32_MAX, cyclic, absl::Milliseconds(37));
  ASSERT_TRUE(routed.Open().ok());
  ASSERT_TRUE(routed.Exchange(Configure()).ok());
  ASSERT_TRUE(routed.Exchange(Command(JW_CMD_ENABLE)).ok());
  uint8_t payload[5];
  ASSERT_EQ(jw_encode_set_target_payload(payload, sizeof(payload), JW_MODE_POSITION, 42), 5);
  ASSERT_TRUE(routed.Exchange({JW_CMD_SET_TARGET, 0, payload, sizeof(payload)}).ok());
  ASSERT_TRUE(routed.Exchange(Command(JW_CMD_GET_FEEDBACK)).ok());
  transport->corrupt_field = 1;
  EXPECT_EQ(routed.Exchange(Command(JW_CMD_GET_FEEDBACK)).status().code(),
            absl::StatusCode::kDataLoss);
  transport->corrupt_field = -1;
  ASSERT_TRUE(routed.Close().ok());
  EXPECT_EQ(cyclic->commands,
            (std::vector<uint8_t>{JW_CMD_SET_TARGET, JW_CMD_GET_FEEDBACK, JW_CMD_GET_FEEDBACK}));
  uint32_t expected = 1;
  for (const auto& bytes : transport->requests) {
    jw_frame_t frame;
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), 0);
    EXPECT_EQ(frame.session_id, 99);
    EXPECT_EQ(frame.message_id, expected++);
  }
  JoshuaWireSession invalid(transport, {}, UINT32_MAX, cyclic);
  EXPECT_EQ(invalid.Open().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(JoshuaWireSessionTest, HandshakeGatesAllCommandsAndPropagatesErrors) {
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(transport->requests.empty());
  ASSERT_TRUE(session.Open().ok());
  auto enable = Exchange(Command(JW_CMD_ENABLE));
  ASSERT_TRUE(enable.ok());
  EXPECT_EQ((*enable)[0], JW_STATUS_ERROR);  // Firmware has not been configured.
  ASSERT_TRUE(Exchange(Configure()).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ENABLE)).ok());
  EXPECT_TRUE(transport->channel.enabled);
  auto identify = Exchange(Command(JW_CMD_IDENTIFY, JW_CHANNEL_NONE));
  ASSERT_TRUE(identify.ok());
  EXPECT_EQ(identify->size(), JW_IDENTIFY_RESPONSE_PAYLOAD_LEN);
  EXPECT_EQ(transport->requests.front()[11], JW_CMD_RESET_SESSION);
}

TEST_F(JoshuaWireSessionTest, EveryCorrelationFieldMustMatch) {
  ASSERT_TRUE(session.Open().ok());
  for (int field = 0; field < 4; ++field) {
    transport->corrupt_field = field;
    EXPECT_EQ(Exchange(Command(JW_CMD_GET_FEEDBACK)).status().code(), absl::StatusCode::kDataLoss);
  }
}

TEST_F(JoshuaWireSessionTest, FullPayloadRoundTrips) {
  // Echo is test-only: prove both session/endpoint boundaries accept the full
  // Maximum JW payload, independent of command payload representations.
  transport->handler = [](void*, const jw_command_t* command, uint8_t* out, size_t cap) -> int {
    if (cap < command->payload_len) return -1;
    memcpy(out, command->payload, command->payload_len);
    return static_cast<int>(command->payload_len);
  };
  ASSERT_TRUE(session.Open().ok());
  const Bytes payload(JW_MAX_PAYLOAD_LEN, 0xa5);
  auto response = session.Exchange({0x40, 3, payload.data(), payload.size()});
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, payload);
  EXPECT_EQ(transport->requests.back().size(), JW_MAX_FRAME_LEN);
}

TEST_F(JoshuaWireSessionTest, InvalidNeutralCommandsDoNotReachTransportOrConsumeIds) {
  ASSERT_TRUE(session.Open().ok());
  const uint8_t byte = 0;
  for (const jw_command_t command :
       {jw_command_t{JW_CMD_SET_TARGET, 0, nullptr, 1},
        jw_command_t{JW_CMD_SET_TARGET, 0, &byte, JW_MAX_PAYLOAD_LEN + 1},
        jw_command_t{JW_CMD_RESET_SESSION, JW_CHANNEL_NONE, nullptr, 0}}) {
    EXPECT_EQ(session.Exchange(command).status().code(), absl::StatusCode::kInvalidArgument);
  }
  ASSERT_EQ(transport->requests.size(), 1);
  ASSERT_TRUE(Exchange(Command(JW_CMD_IDENTIFY, JW_CHANNEL_NONE)).ok());
  jw_frame_t sent;
  ASSERT_EQ(
      jw_decode_frame(transport->requests.back().data(), transport->requests.back().size(), &sent),
      0);
  EXPECT_EQ(sent.message_id, 2);
}

TEST_F(JoshuaWireSessionTest, TimeoutAndLateResponseNeverReuseAnId) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(Exchange(Configure()).ok());
  transport->timeout = true;
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(transport->channel.enabled);  // Request executed; response was lost.
  jw_frame_t timed_out;
  const auto request = transport->requests.back();
  ASSERT_EQ(jw_decode_frame(request.data(), request.size(), &timed_out), 0);
  transport->timeout = false;
  transport->retained.resize(JW_MAX_FRAME_LEN);
  const uint8_t ok = 0;
  const int len = jw_encode_response(
      transport->retained.data(), transport->retained.size(), &timed_out, &ok, 1);
  transport->retained.resize(len);
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDataLoss);
  jw_frame_t next;
  ASSERT_EQ(
      jw_decode_frame(transport->requests.back().data(), transport->requests.back().size(), &next),
      0);
  EXPECT_GT(next.message_id, timed_out.message_id);
  transport->retained.clear();
  EXPECT_TRUE(Exchange(Command(JW_CMD_DISABLE)).ok());
}

TEST_F(JoshuaWireSessionTest, ReconnectAndFirmwareRebootRequireFreshReset) {
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
  jw_endpoint_init(&transport->endpoint);
  EXPECT_EQ(Exchange(Command(JW_CMD_ENABLE)).status().code(), absl::StatusCode::kDeadlineExceeded);
  ASSERT_TRUE(session.Open().ok());
  EXPECT_TRUE(Exchange(Configure()).ok());
}

TEST_F(JoshuaWireSessionTest, FailedResetAndInvalidIdSourceKeepSessionClosed) {
  transport->timeout = true;
  EXPECT_EQ(session.Open().code(), absl::StatusCode::kDeadlineExceeded);
  const auto count = transport->requests.size();
  EXPECT_EQ(Exchange(Configure()).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
  JoshuaWireSession zero(transport, [] { return 0; });
  EXPECT_EQ(zero.Open().code(), absl::StatusCode::kUnavailable);
  transport->timeout = false;
  JoshuaWireSession repeated(transport, [] { return 100; });
  EXPECT_TRUE(repeated.Open().ok());
  EXPECT_EQ(repeated.Open().code(), absl::StatusCode::kUnavailable);
}

TEST_F(JoshuaWireSessionTest, ExhaustionStopsResetsAndRestoresOnlyConfiguration) {
  JoshuaWireSession small(transport, [this] { return ++session_id; }, 5);
  ASSERT_TRUE(small.Open().ok());                                  // ID 1
  ASSERT_TRUE(small.Exchange(Configure()).ok());                   // ID 2
  ASSERT_TRUE(small.Exchange(Command(JW_CMD_ENABLE)).ok());        // ID 3
  ASSERT_TRUE(small.Exchange(Command(JW_CMD_GET_FEEDBACK)).ok());  // ID 4
  auto rotated = small.Exchange(Command(JW_CMD_GET_FEEDBACK));
  EXPECT_EQ(rotated.status().code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_EQ(transport->requests.size(), 7);
  EXPECT_EQ(transport->requests[4][11], JW_CMD_ESTOP);
  EXPECT_EQ(transport->requests[5][11], JW_CMD_RESET_SESSION);
  EXPECT_EQ(transport->requests[6][11], JW_CMD_CONFIGURE_CHANNEL);
  EXPECT_TRUE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.enabled);
  EXPECT_FALSE(transport->channel.estopped);
  EXPECT_TRUE(small.Exchange(Command(JW_CMD_ENABLE)).ok());
  EXPECT_TRUE(transport->channel.enabled);
}

TEST_F(JoshuaWireSessionTest, FailedStopAtExhaustionDoesNotResetOrReuseLastId) {
  JoshuaWireSession small(transport, [this] { return ++session_id; }, 3);
  ASSERT_TRUE(small.Open().ok());
  ASSERT_TRUE(small.Exchange(Configure()).ok());
  transport->timeout = true;
  EXPECT_EQ(small.Exchange(Command(JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  const auto count = transport->requests.size();
  EXPECT_EQ(small.Exchange(Command(JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
}

TEST_F(JoshuaWireSessionTest, ConcurrentCallersSerializeAcrossChannels) {
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
    jw_frame_t frame;
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), 0);
    EXPECT_EQ(frame.message_id, expected++);
  }
}

TEST_F(JoshuaWireSessionTest, EstopStaysLatchedUntilResetAndReconfiguration) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(Exchange(Configure()).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ENABLE)).ok());
  ASSERT_TRUE(Exchange(Command(JW_CMD_ESTOP, JW_CHANNEL_NONE)).ok());
  EXPECT_TRUE(transport->channel.estopped);
  EXPECT_FALSE(transport->channel.enabled);
  auto result = Exchange(Command(JW_CMD_ENABLE));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ((*result)[0], JW_STATUS_ERROR);
  ASSERT_TRUE(session.Open().ok());
  EXPECT_FALSE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.estopped);
}

TEST_F(JoshuaWireSessionTest, ProductionBoardInitializesAndUsesAllSerialChannelCommands) {
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
  config.set_name("serial");
  config.set_board_type(AM243);
  config.set_protocol(JOSHUA_WIRE);
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
// The AM243 EtherCAT artifact uses this exact portable C core. No SDK, clock,
// GPIO or extra test utility is needed to exercise its safety state machine.
uint32_t ProfileU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void ProfilePut32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}

}  // namespace
}  // namespace robot::board
