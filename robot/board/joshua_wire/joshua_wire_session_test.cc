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

jw_command_t Command(uint8_t cmd, uint8_t channel = JW_CHANNEL_NONE) {
  static const uint8_t domain[2] = {};
  return {0,
          0,
          cmd,
          channel,
          cmd == JW_CMD_ESTOP || cmd == JW_CMD_RECOVER ? domain : nullptr,
          cmd == JW_CMD_ESTOP || cmd == JW_CMD_RECOVER ? 2u : 0u};
}
jw_command_t Profile(uint8_t operation, const uint8_t* data = nullptr, size_t size = 0) {
  static thread_local uint8_t payload[JW_MAX_PAYLOAD_LEN];
  jw_profile_request_t profile{JW_PROFILE_ACTUATOR, 1, 0, operation, data, size};
  int length = jw_encode_profile_request(payload, sizeof(payload), &profile);
  return {0, 0, JW_CMD_PROFILE, 0, payload, static_cast<size_t>(length)};
}
jw_command_t Configure() {
  static uint8_t bytes[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 1000;
  jw_encode_configure_step_dir_payload(bytes, sizeof(bytes), &config);
  return Profile(JW_ACTUATOR_CONFIGURE, bytes, sizeof(bytes));
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
      EXPECT_EQ(jw_decode_frame(response.data(), response.size(), &frame), JW_RESULT_OK);
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
    EXPECT_EQ(timeout, absl::Milliseconds(37));
    ++calls;
    return firmware->Exchange(request);
  }
  std::shared_ptr<FirmwareTransport> firmware;
  int calls = 0;
};
TEST_F(JoshuaWireSessionTest, CommonRepliesAndProfileRoutingShareOneIdAllocator) {
  auto cyclic = std::make_shared<RoutedCyclic>(transport);
  JoshuaWireSession routed(
      transport, [] { return 99; }, UINT32_MAX, cyclic, absl::Milliseconds(37));
  ASSERT_TRUE(routed.Open().ok());
  ASSERT_TRUE(routed.Exchange(Configure()).ok());
  ASSERT_TRUE(routed.Exchange(Profile(JW_ACTUATOR_ENABLE)).ok());
  ASSERT_TRUE(routed.Exchange(Profile(JW_ACTUATOR_GET_FEEDBACK)).ok());
  ASSERT_TRUE(routed.Exchange(Command(JW_CMD_GET_STATUS, 0)).ok());
  EXPECT_EQ(cyclic->calls, 1);
  uint32_t expected = 1;
  for (const auto& bytes : transport->requests) {
    jw_frame_t frame{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
    EXPECT_EQ(frame.session_id, 99);
    EXPECT_EQ(frame.message_id, expected++);
  }
}
TEST_F(JoshuaWireSessionTest, UnknownOutcomeRetriesIdenticalBytesAndGatesMutations) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(session.Exchange(Configure()).ok());
  transport->timeout = true;
  EXPECT_EQ(session.Exchange(Profile(JW_ACTUATOR_ENABLE)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(transport->channel.enabled);
  ASSERT_GE(transport->requests.size(), 2);
  EXPECT_EQ(transport->requests.back(), transport->requests[transport->requests.size() - 2]);
  transport->timeout = false;
  auto count = transport->requests.size();
  EXPECT_EQ(session.Exchange(Profile(JW_ACTUATOR_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
  EXPECT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS, 0)).ok());
  EXPECT_TRUE(session.Exchange(Command(JW_CMD_ESTOP)).ok());
  EXPECT_TRUE(transport->channel.estopped);
}
TEST_F(JoshuaWireSessionTest, CorrelationFailureCannotPermitTheNextMutation) {
  ASSERT_TRUE(session.Open().ok());
  transport->corrupt_field = 1;
  EXPECT_EQ(session.Exchange(Command(JW_CMD_IDENTIFY)).status().code(),
            absl::StatusCode::kDataLoss);
  transport->corrupt_field = -1;
  EXPECT_EQ(session.Exchange(Configure()).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(session.Open().ok());
  EXPECT_TRUE(session.Exchange(Configure()).ok());
}
TEST_F(JoshuaWireSessionTest, ReconnectDoesNotClearEstopOrRestoreConfiguration) {
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(session.Exchange(Configure()).ok());
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_ESTOP)).ok());
  ASSERT_TRUE(session.Open().ok());
  EXPECT_TRUE(transport->channel.estopped);
  EXPECT_FALSE(transport->channel.configured);
  EXPECT_FALSE(transport->channel.enabled);
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_RECOVER)).ok());
  EXPECT_FALSE(transport->channel.estopped);
  EXPECT_FALSE(transport->channel.enabled);
}
TEST_F(JoshuaWireSessionTest, ExhaustionStopsAndRequiresExplicitReconciliation) {
  JoshuaWireSession small(
      transport, [this] { return ++session_id; }, 3);
  ASSERT_TRUE(small.Open().ok());
  ASSERT_TRUE(small.Exchange(Configure()).ok());
  EXPECT_EQ(small.Exchange(Profile(JW_ACTUATOR_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(transport->channel.estopped);
  EXPECT_FALSE(transport->channel.enabled);
  auto count = transport->requests.size();
  EXPECT_FALSE(small.Exchange(Configure()).ok());
  EXPECT_EQ(transport->requests.size(), count);
}
TEST_F(JoshuaWireSessionTest, ConcurrentCallersSerializeAcrossTheWholeEndpoint) {
  ASSERT_TRUE(session.Open().ok());
  transport->delay = true;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([this] {
      for (int j = 0; j < 10; ++j)
        EXPECT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS, 0)).ok());
    });
  for (auto& thread : threads) thread.join();
  uint32_t expected = 1;
  for (const auto& bytes : transport->requests) {
    jw_frame_t frame{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
    EXPECT_EQ(frame.message_id, expected++);
  }
}
TEST_F(JoshuaWireSessionTest, InvalidRequestsDoNotConsumeIdsAndFailedResetKeepsClosed) {
  ASSERT_TRUE(session.Open().ok());
  EXPECT_FALSE(session.Exchange({0, 0, JW_CMD_PROFILE, 0, nullptr, 1}).ok());
  EXPECT_FALSE(session.Exchange(Command(JW_CMD_RESET_SESSION)).ok());
  EXPECT_EQ(transport->requests.size(), 1);
  transport->timeout = true;
  EXPECT_FALSE(session.Open().ok());
  auto count = transport->requests.size();
  EXPECT_FALSE(session.Exchange(Configure()).ok());
  EXPECT_EQ(transport->requests.size(), count);
}
uint32_t ProfileU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void ProfilePut32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
}  // namespace
}  // namespace robot::board
