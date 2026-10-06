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
#include "firmware/common/joshua_ethercat_profile.h"
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
    JoshuaService(&channel, requests.size());
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
  JoshuaWireSession small(transport, [this] { return ++session_id; }, 3);
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
TEST_F(JoshuaWireSessionTest, PendingActionRequiresItsOriginalCompletionToken) {
  transport->handler =
      [](void* context, const jw_command_t* command, uint8_t* out, size_t cap) -> int {
    auto* channel = static_cast<JoshuaChannel*>(context);
    const int result = JoshuaCommand(context, command, out, cap);
    jw_profile_request_t operation{};
    if (command->cmd == JW_CMD_PROFILE &&
        jw_decode_profile_request(command->payload, command->payload_len, &operation) ==
            JW_RESULT_OK &&
        operation.operation == JW_ACTUATOR_CONFIGURE) {
      channel->function.state.action_execution = JW_EXECUTION_ACCEPTED;
      return jw_encode_reply_payload(out, cap, JW_STATUS_OK, JW_EXECUTION_ACCEPTED, nullptr, 0);
    }
    return result;
  };
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(session.Exchange(Configure()).ok());
  const auto token = transport->channel.function.state.action_message_id;
  EXPECT_EQ(session.Exchange(Profile(JW_ACTUATOR_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  transport->channel.function.state.action_execution = JW_EXECUTION_COMPLETE;
  ++transport->channel.function.state.action_message_id;
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS, 0)).ok());
  EXPECT_EQ(session.Exchange(Profile(JW_ACTUATOR_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  transport->channel.function.state.action_message_id = token;
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS, 0)).ok());
  ASSERT_TRUE(session.Exchange(Profile(JW_ACTUATOR_ENABLE)).ok());
  EXPECT_TRUE(transport->channel.enabled);
}
TEST_F(JoshuaWireSessionTest, PendingBoardActionGatesEveryFunctionUntilMatchingCompletion) {
  transport->handler =
      [](void* context, const jw_command_t* command, uint8_t* out, size_t cap) -> int {
    const int result = JoshuaCommand(context, command, out, cap);
    if (command->cmd == JW_CMD_ESTOP) {
      static_cast<JoshuaChannel*>(context)->runtime.state.action_execution = JW_EXECUTION_ACCEPTED;
      return jw_encode_reply_payload(out, cap, JW_STATUS_OK, JW_EXECUTION_ACCEPTED, nullptr, 0);
    }
    return result;
  };
  ASSERT_TRUE(session.Open().ok());
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_ESTOP)).ok());
  const auto count = transport->requests.size();
  EXPECT_EQ(session.Exchange(Configure()).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport->requests.size(), count);
  ASSERT_TRUE(session.Exchange(Profile(JW_ACTUATOR_GET_FEEDBACK)).ok());
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS)).ok());
  EXPECT_EQ(session.Exchange(Configure()).status().code(), absl::StatusCode::kFailedPrecondition);
  transport->channel.runtime.state.action_execution = JW_EXECUTION_COMPLETE;
  ASSERT_TRUE(session.Exchange(Command(JW_CMD_GET_STATUS)).ok());
  // The reconciled session may submit again; the firmware still rejects a
  // configure while ESTOP is latched rather than implicitly recovering.
  auto configured = session.Exchange(Configure());
  ASSERT_TRUE(configured.ok());
  EXPECT_EQ((*configured)[0], JW_STATUS_UNAVAILABLE);
}
uint32_t ProfileU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void ProfilePut32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
class Am243EthercatProfileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config.identity.board_id = JW_BOARD_AM243;
    std::memcpy(config.identity.fw_name, "am243-ec-jw", 11);
    config.identity.n_channels = 1;
    config.identity.channel_drives[0] = JW_DRIVE_STEP_DIR;
    std::memcpy(config.artifact, "am243-ec-jw", 11);
    config.context = &channel;
    config.command = JoshuaCommand;
    config.reset = JoshuaReset;
    config.stop = JoshuaStop;
    config.enabled = JoshuaEnabled;
    ASSERT_EQ(JoshuaEthercatProfileInit(&profile, &config, 1000, 500), 0);
    Reset(7);
    JoshuaEthercatProfileTick(&profile, 0, true);
  }
  void Reset(uint32_t session) {
    uint8_t object[8] = {1};
    ProfilePut32(object + 4, session);
    ASSERT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_SESSION_INDEX, object, 8), 0);
    session_id = session;
    next_id = 1;
    generation = 0;
  }
  Bytes Image(bool pdo, uint8_t command, const Bytes& payload = {}, uint8_t channel = 0) {
    Bytes image(pdo ? 80 : 76, 0);
    ProfilePut32(image.data(), session_id);
    ProfilePut32(image.data() + 4, ++generation);
    const size_t offset = pdo ? 16 : 12;
    if (pdo) ProfilePut32(image.data() + 8, ProfileU32(profile.input + 8));
    const int size = jw_encode_frame(image.data() + offset,
                                     64,
                                     session_id,
                                     next_id++,
                                     command,
                                     channel,
                                     payload.data(),
                                     payload.size());
    EXPECT_GT(size, 0);
    image[offset - 4] = size;
    return image;
  }
  Bytes Management(uint8_t command, const Bytes& payload = {}, uint8_t channel = 0) {
    auto request = Image(false, command, payload, channel);
    EXPECT_EQ(
        JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, request.data(), request.size()),
        0);
    jw_frame_t response;
    if (jw_decode_frame(profile.mailbox + 12, profile.mailbox[8], &response) != 0) {
      ADD_FAILURE() << "missing management reply";
      return {};
    }
    Bytes result(response.payload, response.payload + response.payload_len);
    EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, request.data() + 4, 4), 0);
    return result;
  }
  void Enable() {
    EXPECT_EQ(Management(JW_CMD_CONFIGURE_CHANNEL, Bytes(11)), (Bytes{JW_STATUS_OK}));
    EXPECT_EQ(Management(JW_CMD_ENABLE), (Bytes{JW_STATUS_OK}));
    EXPECT_TRUE(channel.enabled);
  }
  Bytes Target(float value) {
    Bytes payload(5);
    EXPECT_EQ(jw_encode_set_target_payload(payload.data(), payload.size(), JW_MODE_POSITION, value),
              5);
    return Image(true, JW_CMD_SET_TARGET, payload);
  }
  JoshuaEthercatProfile profile{};
  JoshuaChannel channel{};
  JoshuaEthercatProfileConfig config{};
  uint32_t session_id = 7;
  uint32_t next_id = 1;
  uint32_t generation = 0;
};

TEST_F(Am243EthercatProfileTest, DescriptorResetReadbackAndEstopAreExact) {
  uint8_t descriptor[36];
  ASSERT_EQ(JoshuaEthercatProfileRead(&profile, JWEC_DESCRIPTOR_INDEX, descriptor, 36), 0);
  EXPECT_EQ(Bytes(descriptor, descriptor + 22),
            (Bytes{'J', 'W', 'E', 'C', 1, 0, 2, 0, 2, 0, 1, 0, 80, 0, 80, 0, 64, 0, 6, 0, 0, 0}));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(descriptor + 22)), "am243-ec-jw");
  EXPECT_EQ(JoshuaEthercatProfileRead(&profile, JWEC_DESCRIPTOR_INDEX, descriptor, 35), -1);
  Enable();
  EXPECT_EQ(Management(JW_CMD_ESTOP, {}, JW_CHANNEL_NONE), (Bytes{JW_STATUS_OK}));
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.estopped);
  uint8_t object[8] = {1, 0, 0, 0, 7};
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_SESSION_INDEX, object, 8), 0);
  EXPECT_TRUE(channel.estopped);  // Same-session write cannot erase the latch/history.
  Reset(8);
  EXPECT_FALSE(channel.estopped);
  EXPECT_FALSE(channel.configured);
  EXPECT_FALSE(channel.enabled);
  ASSERT_EQ(JoshuaEthercatProfileRead(&profile, JWEC_SESSION_INDEX, object, 8), 0);
  EXPECT_EQ(Bytes(object, object + 8), (Bytes{1, 0, 0, 0, 8, 0, 0, 0}));
}

TEST_F(Am243EthercatProfileTest, MailboxRetainedUntilExactAckAndRoutingIsEnforced) {
  auto first = Image(false, JW_CMD_IDENTIFY, {}, JW_CHANNEL_NONE);
  ASSERT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, first.data(), 76), 0);
  Bytes response(profile.mailbox, profile.mailbox + 76);
  auto second = Image(false, JW_CMD_DISABLE);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, second.data(), 76), -1);
  EXPECT_EQ(Bytes(profile.mailbox, profile.mailbox + 76), response);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, second.data() + 4, 4), 0);
  EXPECT_EQ(Bytes(profile.mailbox, profile.mailbox + 76), response);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, first.data() + 4, 4), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4), 0);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, first.data(), 76), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4),
            0);  // Acknowledged duplicate does not resurrect reply.
  auto wrong_plane = Image(false, JW_CMD_GET_FEEDBACK);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, wrong_plane.data(), 76), -1);
  wrong_plane = Image(true, JW_CMD_ENABLE);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, wrong_plane.data(), 80), -1);
}

TEST_F(Am243EthercatProfileTest, PdoExecutesOnceAndDuplicateDoesNotFeedWatchdog) {
  Enable();
  auto target = Target(123.0f);
  ASSERT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), 80), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 123.0f);
  EXPECT_EQ(ProfileU32(profile.input + 4), generation);
  EXPECT_EQ(ProfileU32(profile.input + 8), generation);
  jw_frame_t request, response;
  ASSERT_EQ(jw_decode_frame(target.data() + 16, target[12], &request), 0);
  ASSERT_EQ(jw_decode_frame(profile.input + 16, profile.input[12], &response), 0);
  EXPECT_TRUE(jw_response_matches(&request, &response));
  for (uint64_t now : {100, 200, 300, 400}) {
    JoshuaEthercatProfileTick(&profile, now, true);
    EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), 80), 0);
    EXPECT_EQ(profile.last_progress_us, 0);
    EXPECT_EQ(profile.last_target_us[0], 0);
    EXPECT_TRUE(channel.enabled);
  }
  JoshuaEthercatProfileTick(&profile, 500, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(channel.fault_flags, JOSHUA_ECAT_FAULT_TARGET);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
  EXPECT_EQ(Management(JW_CMD_ENABLE), (Bytes{JW_STATUS_ERROR}));
}

TEST_F(Am243EthercatProfileTest, FeedbackRefreshesProgressButNotTargetAndFreshTargetsFeedBoth) {
  Enable();
  for (uint64_t now : {100, 200, 300, 400}) {
    JoshuaEthercatProfileTick(&profile, now, true);
    auto feedback = Image(true, JW_CMD_GET_FEEDBACK);
    EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, feedback.data(), 80), 0);
    EXPECT_EQ(profile.last_progress_us, now);
    EXPECT_EQ(profile.last_target_us[0], 0);
  }
  JoshuaEthercatProfileTick(&profile, 499, true);
  auto target = Target(42.0f);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), 80), 0);
  JoshuaEthercatProfileTick(&profile, 998, true);
  EXPECT_TRUE(channel.enabled);
  JoshuaEthercatProfileTick(&profile, 999, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(channel.fault_flags, JOSHUA_ECAT_FAULT_TARGET);
}

TEST_F(Am243EthercatProfileTest, CommunicationTimeoutOpLossAndBackwardClockLatchDisabled) {
  profile.comm_timeout_us = 200;
  Enable();
  JoshuaEthercatProfileTick(&profile, 199, true);
  EXPECT_TRUE(channel.enabled);
  JoshuaEthercatProfileTick(&profile, 200, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(channel.fault_flags, JOSHUA_ECAT_FAULT_COMM);
  Reset(8);
  Enable();
  JoshuaEthercatProfileTick(&profile, 201, false);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(channel.fault_flags, JOSHUA_ECAT_FAULT_STATE);
  Reset(9);
  EXPECT_EQ(Management(JW_CMD_CONFIGURE_CHANNEL, Bytes(11)), (Bytes{0}));
  EXPECT_EQ(Management(JW_CMD_ENABLE), (Bytes{JW_STATUS_ERROR}));  // Cannot enable outside OP.
  JoshuaEthercatProfileTick(&profile, 202, true);
  EXPECT_EQ(Management(JW_CMD_ENABLE), (Bytes{0}));
  JoshuaEthercatProfileTick(&profile, 201, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.fault_flags & JOSHUA_ECAT_FAULT_STATE);
}

TEST_F(Am243EthercatProfileTest, CancellationStopMalformedAndForeignSessionImages) {
  Enable();
  Bytes cancel(80, 0);
  ProfilePut32(cancel.data(), 7);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, cancel.data(), 80), 0);
  EXPECT_TRUE(channel.enabled);  // Session-bearing generation zero cancels only the slot.
  auto stale = Target(99.0f);
  ProfilePut32(stale.data(), 99);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, stale.data(), 80), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
  Bytes stop(80, 0);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, stop.data(), 80), 0);
  EXPECT_FALSE(channel.enabled);
  Reset(8);
  Enable();
  auto malformed = Target(12.0f);
  malformed.back() = 1;
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, malformed.data(), 80), -1);
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.fault_flags & JOSHUA_ECAT_FAULT_PROTOCOL);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, malformed.data(), 79), -1);
}

TEST_F(Am243EthercatProfileTest, MessageIdsAreGlobalAcrossPlanesAndResetClearsBothReplies) {
  Enable();
  auto target = Target(50.0f);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), 80), 0);
  --next_id;  // Reuse PDO message ID through CoE with a new transport generation.
  auto reused = Image(false, JW_CMD_DISABLE);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, reused.data(), 76), -1);
  EXPECT_TRUE(channel.enabled);
  Reset(8);
  EXPECT_EQ(ProfileU32(profile.input), 8);
  EXPECT_EQ(ProfileU32(profile.input + 4), 0);
  EXPECT_EQ(ProfileU32(profile.input + 8), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4), 0);
  EXPECT_EQ(profile.session.last_message_id, 0);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), 80), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
}

TEST_F(Am243EthercatProfileTest, RejectsIncompletePortConfigurationBeforeCallingBackend) {
  auto invalid = config;
  invalid.stop = nullptr;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  invalid = config;
  invalid.identity.n_channels = JW_MAX_CHANNELS + 1;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  invalid = config;
  invalid.artifact[1] = 0;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &config, 0, 500), -1);
  EXPECT_EQ(profile.session.session_id, 7);  // Invalid init did not erase live state.
}

TEST_F(Am243EthercatProfileTest, IndependentChannelsCannotKeepEachOthersTargetsAlive) {
  // Test-local two-channel drive backend, using the existing software channel
  // semantics. The shared profile knows neither the model nor this storage.
  JoshuaChannel channels[2]{};
  config.context = channels;
  config.identity.board_id = JW_BOARD_TEENSY41;
  config.identity.n_channels = 2;
  config.identity.channel_drives[1] = JW_DRIVE_STEP_DIR;
  std::memset(config.identity.fw_name, 0, sizeof(config.identity.fw_name));
  std::memcpy(config.identity.fw_name, "other-board-jw", 14);
  std::memset(config.artifact, 0, sizeof(config.artifact));
  std::memcpy(config.artifact, "other-ec-jw", 12);
  config.reset = [](void* context) {
    auto* c = static_cast<JoshuaChannel*>(context);
    JoshuaReset(&c[0]);
    JoshuaReset(&c[1]);
  };
  config.stop = [](void* context, uint16_t faults) {
    auto* c = static_cast<JoshuaChannel*>(context);
    JoshuaStop(&c[0], faults);
    JoshuaStop(&c[1], faults);
  };
  config.enabled = [](void* context, uint8_t index) {
    return static_cast<JoshuaChannel*>(context)[index].enabled;
  };
  config.command = [](void* context, const jw_command_t* cmd, uint8_t* out, size_t capacity) {
    auto* c = static_cast<JoshuaChannel*>(context);
    if (cmd->cmd == JW_CMD_ESTOP) {
      JoshuaCommand(&c[0], cmd, out, capacity);
      return JoshuaCommand(&c[1], cmd, out, capacity);
    }
    auto local = *cmd;
    local.channel = 0;
    return JoshuaCommand(&c[cmd->channel], &local, out, capacity);
  };
  ASSERT_EQ(JoshuaEthercatProfileInit(&profile, &config, 1000, 500), 0);
  Reset(11);
  JoshuaEthercatProfileTick(&profile, 0, true);
  auto identity = Management(JW_CMD_IDENTIFY, {}, JW_CHANNEL_NONE);
  jw_identify_response_t decoded{};
  ASSERT_EQ(jw_decode_identify_payload(identity.data(), identity.size(), &decoded), 0);
  EXPECT_EQ(decoded.board_id, JW_BOARD_TEENSY41);
  EXPECT_EQ(decoded.n_channels, 2);
  EXPECT_EQ(std::string(decoded.fw_name), "other-board-jw");
  for (uint8_t i = 0; i < 2; ++i) {
    EXPECT_EQ(Management(JW_CMD_CONFIGURE_CHANNEL, Bytes(11), i), Bytes{JW_STATUS_OK});
    EXPECT_EQ(Management(JW_CMD_ENABLE, {}, i), Bytes{JW_STATUS_OK});
  }
  EXPECT_EQ(Management(JW_CMD_ENABLE, {}, 2), Bytes{JW_STATUS_ERROR});
  JoshuaEthercatProfileTick(&profile, 400, true);
  auto fresh = Target(42);
  ASSERT_EQ(JoshuaEthercatProfilePdo(&profile, fresh.data(), fresh.size()), 0);
  EXPECT_EQ(profile.last_target_us[0], 400);
  EXPECT_EQ(profile.last_target_us[1], 0);
  JoshuaEthercatProfileTick(&profile, 500, true);
  for (const auto& c : channels) {
    EXPECT_FALSE(c.enabled);
    EXPECT_EQ(c.fault_flags, JOSHUA_ECAT_FAULT_TARGET);
    EXPECT_FLOAT_EQ(c.target_value, 0);
  }
  // The profile itself enforces the latch even if a backend loses its latch.
  channels[0].estopped = false;
  EXPECT_EQ(Management(JW_CMD_ENABLE), Bytes{JW_STATUS_ERROR});
  Reset(12);
  EXPECT_FALSE(profile.fault_latched);
  for (const auto& c : channels) EXPECT_FALSE(c.configured);
}

}  // namespace
}  // namespace robot::board
