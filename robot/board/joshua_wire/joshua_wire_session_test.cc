// Hardware-free host session and production JoshuaWireBoard integration tests.
// A loopback transport runs the real AM243 software command handler and shared
// firmware endpoint to test correlation, failure/reconnect, ID exhaustion,
// concurrent callers, ESTOP and teardown without opening a physical device.
#include "robot/board/joshua_wire/joshua_wire_session.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "firmware/am243/joshua_dual_transport/src/am243_software_backend.h"
#include "firmware/common/joshua_ethercat_profile.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/common/soes/joshua_ethercat_soes.h"
#include "gtest/gtest.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/comm/factory/comm_factory.h"

extern "C" {
#include "ecat_slv.h"
#include "esc_coe.h"
}

// Memory-only ESC for the real SOES parser/PDO callbacks. This substitutes only
// registers, not the slave stack; it cannot validate AM243 PRU handshakes/timing.
namespace {
std::array<uint8_t, 0x2000> soes_esc{};
uint64_t soes_time_us;
std::function<void()> soes_after_admission;
decltype(ESCvar.pre_object_download_hook) soes_before_download;
}  // namespace
extern "C" void ESC_read(uint16_t address, void* data, uint16_t size) {
  ASSERT_LE(static_cast<size_t>(address) + size, soes_esc.size());
  std::memcpy(data, soes_esc.data() + address, size);
  if (address == SM2_sma) ESCvar.ALevent &= ~ESCREG_ALEVENT_SM2;
}
extern "C" void ESC_write(uint16_t address, void* data, uint16_t size) {
  ASSERT_LE(static_cast<size_t>(address) + size, soes_esc.size());
  std::memcpy(soes_esc.data() + address, data, size);
}
extern "C" void ESC_init(const esc_cfg_t*) {}
extern "C" int8_t EEP_read(uint32_t, uint8_t*, uint16_t) {
  return 1;
}
extern "C" int8_t EEP_write(uint32_t, uint8_t*, uint16_t) {
  return 1;
}

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
constexpr uint8_t kConfigure = 0x80, kSetTarget = 0x81, kFeedback = 0x82, kEnable = 0x83,
                  kDisable = 0x84;
class Am243EthercatProfileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    JoshuaInit(&channel, 123, JW_PLANE_MESSAGE | JW_PLANE_CYCLIC);
    config.identity = channel.runtime.identity;
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
    Bytes body = payload;
    if (command >= kConfigure && command <= kDisable) {
      if (command == kConfigure && body == Bytes(11)) {
        body[0] = 0xe8;
        body[1] = 3;
      }
      uint8_t bytes[JW_MAX_PAYLOAD_LEN];
      jw_profile_request_t profile{JW_PROFILE_ACTUATOR,
                                   1,
                                   0,
                                   static_cast<uint8_t>(command - 0x80),
                                   body.data(),
                                   body.size()};
      const int len = jw_encode_profile_request(bytes, sizeof(bytes), &profile);
      body = Bytes(bytes, bytes + len);
      command = JW_CMD_PROFILE;
    } else if (command == JW_CMD_ESTOP || command == JW_CMD_RECOVER)
      body = Bytes(2);
    Bytes image(pdo ? JWEC_PDO_SIZE : JWEC_MAILBOX_SIZE, 0);
    ProfilePut32(image.data(), session_id);
    ProfilePut32(image.data() + 4, ++generation);
    const size_t offset = pdo ? 16 : 12;
    if (pdo) ProfilePut32(image.data() + 8, ProfileU32(profile.input + 8));
    const int size = jw_encode_frame(image.data() + offset,
                                     JW_MAX_FRAME_LEN,
                                     session_id,
                                     next_id++,
                                     command,
                                     channel,
                                     body.data(),
                                     body.size());
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
    if (jw_decode_frame(profile.mailbox + 12, profile.mailbox[8], &response) != JW_RESULT_OK) {
      ADD_FAILURE() << "missing management reply";
      return {};
    }
    Bytes result(response.payload, response.payload + response.payload_len);
    EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, request.data() + 4, 4), 0);
    return result;
  }
  void Enable() {
    if (profile.fault_latched || channel.estopped)
      EXPECT_EQ(Management(JW_CMD_RECOVER, {}, JW_CHANNEL_NONE),
                (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
    EXPECT_EQ(Management(kConfigure, Bytes(11)), ((Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE})));
    EXPECT_EQ(Management(kEnable), ((Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE})));
    EXPECT_TRUE(channel.enabled);
  }
  Bytes Target(float value) {
    Bytes payload(5);
    EXPECT_EQ(jw_encode_set_target_payload(payload.data(), payload.size(), JW_MODE_POSITION, value),
              5);
    return Image(true, kSetTarget, payload);
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
            (Bytes{'J',
                   'W',
                   'E',
                   'C',
                   1,
                   0,
                   JW_PROTO_VERSION,
                   0,
                   JW_PROTO_VERSION,
                   0,
                   JWEC_LAYOUT_VERSION,
                   0,
                   JWEC_PDO_SIZE,
                   0,
                   JWEC_PDO_SIZE,
                   0,
                   JW_MAX_FRAME_LEN,
                   0,
                   6,
                   0,
                   0,
                   0}));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(descriptor + 22)), "am243-ec-jw");
  EXPECT_EQ(JoshuaEthercatProfileRead(&profile, JWEC_DESCRIPTOR_INDEX, descriptor, 35), -1);
  Enable();
  EXPECT_EQ(Management(JW_CMD_ESTOP, {}, JW_CHANNEL_NONE),
            ((Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE})));
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.estopped);
  uint8_t object[8] = {1, 0, 0, 0, 7};
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_SESSION_INDEX, object, 8), 0);
  EXPECT_TRUE(channel.estopped);  // Same-session write cannot erase the latch/history.
  Reset(8);
  EXPECT_TRUE(channel.estopped);
  EXPECT_FALSE(channel.configured);
  EXPECT_FALSE(channel.enabled);
  ASSERT_EQ(JoshuaEthercatProfileRead(&profile, JWEC_SESSION_INDEX, object, 8), 0);
  EXPECT_EQ(Bytes(object, object + 8), (Bytes{1, 0, 0, 0, 8, 0, 0, 0}));
}

TEST_F(Am243EthercatProfileTest, MailboxRetainedUntilExactAckAndRoutingIsEnforced) {
  auto first = Image(false, JW_CMD_IDENTIFY, {}, JW_CHANNEL_NONE);
  ASSERT_EQ(
      JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, first.data(), JWEC_MAILBOX_SIZE), 0);
  Bytes response(profile.mailbox, profile.mailbox + JWEC_MAILBOX_SIZE);
  auto second = Image(false, kDisable);
  EXPECT_EQ(
      JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, second.data(), JWEC_MAILBOX_SIZE),
      -1);
  EXPECT_EQ(Bytes(profile.mailbox, profile.mailbox + JWEC_MAILBOX_SIZE), response);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, second.data() + 4, 4), 0);
  EXPECT_EQ(Bytes(profile.mailbox, profile.mailbox + JWEC_MAILBOX_SIZE), response);
  EXPECT_EQ(JoshuaEthercatProfileWrite(&profile, JWEC_ACK_INDEX, first.data() + 4, 4), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4), 0);
  EXPECT_EQ(
      JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, first.data(), JWEC_MAILBOX_SIZE), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4),
            0);  // Acknowledged duplicate does not resurrect reply.
  auto wrong_plane = Image(false, kFeedback);
  EXPECT_EQ(JoshuaEthercatProfileWrite(
                &profile, JWEC_REQUEST_INDEX, wrong_plane.data(), JWEC_MAILBOX_SIZE),
            -1);
  wrong_plane = Image(true, kEnable);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, wrong_plane.data(), JWEC_PDO_SIZE), -1);
}

TEST_F(Am243EthercatProfileTest, PdoExecutesOnceAndDuplicateDoesNotFeedWatchdog) {
  Enable();
  auto target = Target(123.0f);
  ASSERT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), JWEC_PDO_SIZE), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 123.0f);
  EXPECT_EQ(ProfileU32(profile.input + 4), generation);
  EXPECT_EQ(ProfileU32(profile.input + 8), generation);
  jw_frame_t request, response;
  ASSERT_EQ(jw_decode_frame(target.data() + 16, target[12], &request), JW_RESULT_OK);
  ASSERT_EQ(jw_decode_frame(profile.input + 16, profile.input[12], &response), JW_RESULT_OK);
  EXPECT_TRUE(jw_response_matches(&request, &response));
  for (uint64_t now : {100, 200, 300, 400}) {
    JoshuaEthercatProfileTick(&profile, now, true);
    EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), JWEC_PDO_SIZE), 0);
    EXPECT_EQ(profile.last_progress_us, 0);
    EXPECT_EQ(profile.last_target_us[0], 0);
    EXPECT_TRUE(channel.enabled);
  }
  JoshuaEthercatProfileTick(&profile, 500, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(channel.fault_flags, JOSHUA_ECAT_FAULT_TARGET);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
  EXPECT_EQ(Management(kEnable), ((Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED})));
}

TEST_F(Am243EthercatProfileTest, FeedbackRefreshesProgressButNotTargetAndFreshTargetsFeedBoth) {
  Enable();
  for (uint64_t now : {100, 200, 300, 400}) {
    JoshuaEthercatProfileTick(&profile, now, true);
    auto feedback = Image(true, kFeedback);
    EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, feedback.data(), JWEC_PDO_SIZE), 0);
    EXPECT_EQ(profile.last_progress_us, now);
    EXPECT_EQ(profile.last_target_us[0], 0);
  }
  JoshuaEthercatProfileTick(&profile, 499, true);
  auto target = Target(42.0f);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), JWEC_PDO_SIZE), 0);
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
  EXPECT_TRUE(channel.estopped);
  EXPECT_EQ(Management(JW_CMD_RECOVER, {}, JW_CHANNEL_NONE),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(Management(kConfigure, Bytes(11)), ((Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE})));
  EXPECT_EQ(Management(kEnable),
            ((Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED})));  // Cannot enable outside OP.
  JoshuaEthercatProfileTick(&profile, 202, true);
  EXPECT_EQ(Management(kEnable), ((Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE})));
  JoshuaEthercatProfileTick(&profile, 201, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.fault_flags & JOSHUA_ECAT_FAULT_STATE);
}

TEST_F(Am243EthercatProfileTest, CancellationStopMalformedAndForeignSessionImages) {
  Enable();
  Bytes cancel(JWEC_PDO_SIZE, 0);
  ProfilePut32(cancel.data(), 7);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, cancel.data(), JWEC_PDO_SIZE), 0);
  EXPECT_TRUE(channel.enabled);  // Session-bearing generation zero cancels only the slot.
  auto stale = Target(99.0f);
  ProfilePut32(stale.data(), 99);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, stale.data(), JWEC_PDO_SIZE), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
  Bytes stop(JWEC_PDO_SIZE, 0);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, stop.data(), JWEC_PDO_SIZE), 0);
  EXPECT_FALSE(channel.enabled);
  Reset(8);
  Enable();
  auto malformed = Target(12.0f);
  malformed.back() = 1;
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, malformed.data(), JWEC_PDO_SIZE), -1);
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(channel.fault_flags & JOSHUA_ECAT_FAULT_PROTOCOL);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, malformed.data(), 79), -1);
}

TEST_F(Am243EthercatProfileTest, MessageIdsAreGlobalAcrossPlanesAndResetClearsBothReplies) {
  Enable();
  auto target = Target(50.0f);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), JWEC_PDO_SIZE), 0);
  --next_id;  // Reuse PDO message ID through CoE with a new transport generation.
  auto reused = Image(false, kDisable);
  EXPECT_EQ(
      JoshuaEthercatProfileWrite(&profile, JWEC_REQUEST_INDEX, reused.data(), JWEC_MAILBOX_SIZE),
      -1);
  EXPECT_TRUE(channel.enabled);
  Reset(8);
  EXPECT_EQ(ProfileU32(profile.input), 8);
  EXPECT_EQ(ProfileU32(profile.input + 4), 0);
  EXPECT_EQ(ProfileU32(profile.input + 8), 0);
  EXPECT_EQ(ProfileU32(profile.mailbox + 4), 0);
  EXPECT_EQ(profile.session.last_message_id, 0);
  EXPECT_EQ(JoshuaEthercatProfilePdo(&profile, target.data(), JWEC_PDO_SIZE), 0);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
}

TEST_F(Am243EthercatProfileTest, RejectsIncompletePortConfigurationBeforeCallingBackend) {
  auto invalid = config;
  invalid.stop = nullptr;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  invalid = config;
  invalid.identity.function_count = JW_MAX_FUNCTIONS + 1;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  invalid = config;
  invalid.artifact[1] = 0;
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &invalid, 1000, 500), -1);
  EXPECT_EQ(JoshuaEthercatProfileInit(&profile, &config, 0, 500), -1);
  EXPECT_EQ(profile.session.session_id, 7);  // Invalid init did not erase live state.
}

TEST_F(Am243EthercatProfileTest, SparseFunctionIdsHaveIndependentWatchdogSlots) {
  channel.function.descriptor.function_id = 200;
  config.command = [](void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
    return JoshuaCommand(context, command, out, cap);
  };
  config.enabled = [](void* context, uint8_t function) {
    return function == 200 && static_cast<JoshuaChannel*>(context)->enabled;
  };
  ASSERT_EQ(JoshuaEthercatProfileInit(&profile, &config, 1000, 500), 0);
  Reset(8);
  JoshuaEthercatProfileTick(&profile, 0, true);
  EXPECT_EQ(Management(kConfigure, Bytes(11), 200), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(Management(kEnable, {}, 200), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_TRUE(channel.enabled);
  EXPECT_EQ(profile.last_target_us[200], 0);
  JoshuaEthercatProfileTick(&profile, 500, true);
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(profile.fault_latched);
}

// Extend the maintained profile suite with the production SOES parser, fixed
// object dictionary and real PDO packing. No separate development probe files.
class SoesProfileTest : public Am243EthercatProfileTest {
 protected:
  void SetUp() override {
    Am243EthercatProfileTest::SetUp();
    soes_esc.fill(0);
    soes_esc[ESCREG_DLSTATUS] = 1;
    soes_time_us = 0;
    std::memset(static_cast<void*>(&ESCvar), 0, sizeof(ESCvar));
    std::memset(MBXcontrol, 0, sizeof(_MBXcontrol) * MBXBUFFERS);
    JoshuaSoesConfig port{};
    port.profile = &profile;
    port.vendor_id = 0x12345678;
    port.product_code = 0xabcdef01;
    port.time_us = []() -> uint64_t { return soes_time_us; };
    port.lock = []() -> uintptr_t { return 0; };
    port.unlock = [](uintptr_t) {};
    ASSERT_EQ(JoshuaSoesInit(&port), 0);
    soes_after_admission = {};
    soes_before_download = ESCvar.pre_object_download_hook;
    ESCvar.pre_object_download_hook =
        [](uint16_t index, uint8_t sub, void* data, size_t size, uint16_t flags) -> uint32_t {
      const auto result = soes_before_download(index, sub, data, size, flags);
      if (result == 0 && soes_after_admission) {
        auto hook = std::exchange(soes_after_admission, {});
        hook();  // Preempt the real parser between admission and copy/dispatch.
      }
      return result;
    };
    ESCvar.ALstatus = ESCpreop;
    ESCvar.MBXrun = 1;
    // Initialize SOES's mailbox pointers through its state machine config.
    ESCvar.activembxsize = MBXSIZE;
    Bytes reset(8);
    reset[0] = 1;
    ProfilePut32(reset.data() + 4, 8);
    EXPECT_EQ(Download(JWEC_SESSION_INDEX, reset)[8], 0x60);
    session_id = 8;
    generation = 0;
    next_id = 1;
  }
  Bytes Transfer(uint8_t command,
                 uint16_t index,
                 uint8_t sub,
                 const Bytes& payload,
                 uint32_t declared_size = 0) {
    std::memset(MBX, 0, MBXBUFFERS * MBXSIZE);
    std::memset(MBXcontrol, 0, sizeof(_MBXcontrol) * MBXBUFFERS);
    auto* sdo = reinterpret_cast<_COEsdo*>(MBX);
    sdo->mbxheader.length = COE_HEADERSIZE + payload.size();
    sdo->mbxheader.mbxtype = MBXCOE;
    sdo->coeheader.numberservice = COE_SDOREQUEST << 12;
    sdo->command = command;
    sdo->index = index;
    sdo->subindex = sub;
    sdo->size = declared_size;
    if (!payload.empty()) std::memcpy(MBX + sizeof(_COEsdo), payload.data(), payload.size());
    MBXcontrol[0].state = MBXstate_inclaim;
    ESCvar.xoe = 0;
    ESC_coeprocess();
    for (size_t i = 1; i < MBXBUFFERS; ++i) {
      if (MBXcontrol[i].state == MBXstate_outreq) {
        auto* reply = reinterpret_cast<_COEsdo*>(MBX + i * MBXSIZE);
        const auto* begin = reinterpret_cast<const uint8_t*>(reply);
        return Bytes(begin, begin + sizeof(_MBXh) + reply->mbxheader.length);
      }
    }
    ADD_FAILURE() << "SOES did not return a CoE response";
    return Bytes(16);
  }
  Bytes Download(uint16_t index, const Bytes& bytes, uint8_t sub = 0, bool ca = false) {
    // Normal transfers exercise the full-size parser, even for the 4-byte ack.
    return Transfer(0x21 | (ca ? 0x10 : 0), index, sub, bytes, bytes.size());
  }
  Bytes Upload(uint16_t index, uint8_t sub = 0, bool ca = false) {
    return Transfer(0x40 | (ca ? 0x10 : 0), index, sub, {});
  }
  Bytes CoeCommand(uint8_t command, const Bytes& payload = {}, uint8_t function = 0) {
    const auto image = Image(false, command, payload, function);
    EXPECT_EQ(Download(JWEC_REQUEST_INDEX, image)[8], 0x60);
    const auto uploaded = Upload(JWEC_RESPONSE_INDEX);
    if (uploaded.size() != 16 + JWEC_MAILBOX_SIZE) {
      ADD_FAILURE() << "SOES did not return a complete JW mailbox image";
      return {};
    }
    jw_frame_t reply;
    if (jw_decode_frame(uploaded.data() + 28, uploaded[24], &reply) != JW_RESULT_OK) {
      ADD_FAILURE() << "SOES did not return a JW command reply";
      return {};
    }
    const Bytes result(reply.payload, reply.payload + reply.payload_len);
    EXPECT_EQ(Download(JWEC_ACK_INDEX, Bytes(image.begin() + 4, image.begin() + 8))[8], 0x60);
    return result;
  }
  void SetOp() {
    ESCvar.ALstatus = ESCop;
    ESCvar.App.state = APPSTATE_INPUT | APPSTATE_OUTPUT;
    ESCvar.ESC_SM2_sml = sizeOfPDO(0x1c12, &ESCvar.sm2mappings, SMmap2, MAX_MAPPINGS_SM2);
    ESCvar.ESC_SM3_sml = sizeOfPDO(0x1c13, &ESCvar.sm3mappings, SMmap3, MAX_MAPPINGS_SM3);
    ASSERT_EQ(ESCvar.ESC_SM2_sml, JWEC_PDO_SIZE);
    ASSERT_EQ(ESCvar.ESC_SM3_sml, JWEC_PDO_SIZE);
  }
  Bytes Cyclic(const Bytes& bytes) {
    std::memcpy(soes_esc.data() + SM2_sma, bytes.data(), bytes.size());
    ESCvar.ALevent |= ESCREG_ALEVENT_SM2;
    DIG_process(DIG_PROCESS_OUTPUTS_FLAG | DIG_PROCESS_APP_HOOK_FLAG | DIG_PROCESS_INPUTS_FLAG);
    return Bytes(soes_esc.data() + SM3_sma, soes_esc.data() + SM3_sma + JWEC_PDO_SIZE);
  }
  void ConfigureAndEnable() {
    SetOp();
    if (profile.fault_latched || channel.estopped) {
      const auto recovery = Image(false, JW_CMD_RECOVER, {}, JW_CHANNEL_NONE);
      ASSERT_EQ(Download(JWEC_REQUEST_INDEX, recovery)[8], 0x60);
      ASSERT_EQ(Download(JWEC_ACK_INDEX, Bytes(recovery.begin() + 4, recovery.begin() + 8))[8],
                0x60);
    }
    for (uint8_t cmd : {kConfigure, kEnable}) {
      const auto image = Image(false, cmd, cmd == kConfigure ? Bytes(11) : Bytes{});
      EXPECT_EQ(Download(JWEC_REQUEST_INDEX, image)[8], 0x60);
      const auto uploaded = Upload(JWEC_RESPONSE_INDEX);
      ASSERT_EQ(uploaded.size(), 16 + JWEC_MAILBOX_SIZE);
      EXPECT_EQ(uploaded[8], 0x41);
      jw_frame_t reply;
      ASSERT_EQ(jw_decode_frame(uploaded.data() + 28, uploaded[24], &reply), JW_RESULT_OK);
      ASSERT_EQ(reply.payload_len, 2);
      EXPECT_EQ(reply.payload[0], JW_STATUS_OK);
      EXPECT_EQ(Download(JWEC_ACK_INDEX, Bytes(image.begin() + 4, image.begin() + 8))[8], 0x60);
    }
    ASSERT_TRUE(channel.enabled);
  }
  void ExpectRetiredRecovery(std::function<void()> stop) {
    const uint32_t last_message = profile.session.last_message_id;
    soes_after_admission = std::move(stop);
    const auto recovery = Image(false, JW_CMD_RECOVER, {}, JW_CHANNEL_NONE);
    EXPECT_EQ(Download(JWEC_REQUEST_INDEX, recovery)[8], 0x80);
    EXPECT_EQ(profile.session.last_message_id, last_message);
    EXPECT_TRUE(profile.fault_latched);
    EXPECT_TRUE(channel.estopped);
    EXPECT_FALSE(channel.configured);
    EXPECT_FALSE(channel.enabled);
    // Only a newly admitted post-stop recovery may clear the newer latch.
    EXPECT_EQ(CoeCommand(JW_CMD_RECOVER, {}, JW_CHANNEL_NONE),
              (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
    EXPECT_FALSE(profile.fault_latched);
    EXPECT_FALSE(channel.estopped);
    EXPECT_FALSE(channel.enabled);
  }
};

TEST_F(SoesProfileTest, IndependentWatchdogRetiresAdmittedRecovery) {
  ConfigureAndEnable();
  ExpectRetiredRecovery([&] {
    soes_time_us = 1000;
    JoshuaEthercatProfileTick(&profile, soes_time_us, true);
    ASSERT_TRUE(profile.fault_latched);
  });
}

TEST_F(SoesProfileTest, ExpiryAtDispatchRetiresAdmittedRecovery) {
  ConfigureAndEnable();
  ExpectRetiredRecovery([] { soes_time_us = 1000; });
}

TEST_F(SoesProfileTest, RepeatedStopWhileFaultedRetiresAdmittedRecovery) {
  ConfigureAndEnable();
  JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_COMM);
  ExpectRetiredRecovery([&] { JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_COMM); });
}

TEST_F(SoesProfileTest, StateLossWhileFaultedRetiresAdmittedRecovery) {
  ConfigureAndEnable();
  JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_COMM);
  ExpectRetiredRecovery([] { ESC_stopoutput(); });
}

TEST_F(SoesProfileTest, IndependentWatchdogRetiresAdmittedSessionReset) {
  ConfigureAndEnable();
  soes_after_admission = [&] {
    soes_time_us = 1000;
    JoshuaEthercatProfileTick(&profile, soes_time_us, true);
  };
  Bytes reset(JWEC_SESSION_SIZE);
  ProfilePut32(reset.data(), JWEC_RESET_OPERATION);
  ProfilePut32(reset.data() + 4, 9);
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, reset)[8], 0x80);
  EXPECT_EQ(profile.session.session_id, session_id);
  EXPECT_TRUE(profile.fault_latched);
  EXPECT_TRUE(channel.estopped);
  // A reset admitted after the stop is still allowed and preserves the latch.
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, reset)[8], 0x60);
  EXPECT_EQ(profile.session.session_id, 9u);
  EXPECT_TRUE(profile.fault_latched);
  EXPECT_TRUE(channel.estopped);
}

TEST_F(SoesProfileTest, RealCoeDiscoveryAndExactAccessContract) {
  const auto descriptor = Upload(JWEC_DESCRIPTOR_INDEX);
  ASSERT_EQ(descriptor.size(), 52);
  EXPECT_EQ(descriptor[8], 0x41);
  EXPECT_EQ(std::memcmp(descriptor.data() + 16, "JWEC", 4), 0);
  EXPECT_EQ(ProfileU32(Upload(0x1018, 1).data() + 12), 0x12345678);
  EXPECT_EQ(ProfileU32(Upload(0x1018, 2).data() + 12), 0xabcdef01);
  EXPECT_EQ(Upload(0x1c12, 1)[12], 0);
  EXPECT_EQ(Upload(0x1c12, 1)[13], 0x16);
  EXPECT_EQ(ProfileU32(Upload(0x1600, JWEC_PDO_SIZE / 4).data() + 12), 0x70001c20);
  EXPECT_EQ(ProfileU32(Upload(JWEC_SESSION_INDEX).data() + 20), session_id);
  EXPECT_EQ(Upload(JWEC_DESCRIPTOR_INDEX, 0, true)[8], 0x80);
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, Bytes(7))[8], 0x80);
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, Bytes(9))[8], 0x80);
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, Bytes(8), 1)[8], 0x80);
  EXPECT_EQ(Download(JWEC_SESSION_INDEX, Bytes(8), 0, true)[8], 0x80);
  EXPECT_EQ(Download(JWEC_DESCRIPTOR_INDEX, Bytes(36))[8], 0x80);
  EXPECT_EQ(Download(0x7000, Bytes(4), 1)[8], 0x80);
  EXPECT_EQ(Upload(0x6000, 1)[8], 0x80);
  // No partial session mutation, truncation or segmented command execution.
  EXPECT_EQ(Transfer(0x21, JWEC_SESSION_INDEX, 0, Bytes(4), 8)[8], 0x80);
  EXPECT_EQ(Transfer(0x21, JWEC_SESSION_INDEX, 0, Bytes(8), 0x10008)[8], 0x80);
  EXPECT_EQ(ProfileU32(Upload(JWEC_SESSION_INDEX).data() + 20), session_id);
}

TEST_F(SoesProfileTest, RealPdoMappingDuplicateWatchdogAndOpLoss) {
  ConfigureAndEnable();
  const auto target = Target(42);
  const auto first = Cyclic(target);
  EXPECT_FLOAT_EQ(channel.target_value, 42);
  for (soes_time_us = 100; soes_time_us < 500; soes_time_us += 100) {
    EXPECT_EQ(Cyclic(target), first);
    EXPECT_TRUE(channel.enabled);
  }
  Cyclic(target);
  EXPECT_FALSE(channel.enabled);
  EXPECT_NE(channel.fault_flags & JOSHUA_ECAT_FAULT_TARGET, 0);
  Bytes reset(8);
  reset[0] = 1;
  ProfilePut32(reset.data() + 4, 9);
  ASSERT_EQ(Download(JWEC_SESSION_INDEX, reset)[8], 0x60);
  session_id = 9;
  next_id = 1;
  ConfigureAndEnable();
  ESC_stopoutput();
  EXPECT_FALSE(channel.enabled);
  EXPECT_TRUE(profile.fault_latched);
}

TEST_F(SoesProfileTest, SharedActuatorGuardsAndEstopStatusReachBothPlanes) {
  ConfigureAndEnable();
  EXPECT_EQ(CoeCommand(kConfigure, Bytes(11)), (Bytes{JW_STATUS_BUSY, JW_EXECUTION_REJECTED}));
  EXPECT_TRUE(channel.configured);
  EXPECT_TRUE(channel.enabled);

  Bytes unsupported_target(JW_SET_TARGET_PAYLOAD_LEN);
  ASSERT_EQ(jw_encode_set_target_payload(
                unsupported_target.data(), unsupported_target.size(), JW_MODE_TORQUE, 42),
            JW_SET_TARGET_PAYLOAD_LEN);
  const auto input = Cyclic(Image(true, kSetTarget, unsupported_target));
  jw_frame_t reply;
  ASSERT_EQ(jw_decode_frame(input.data() + 16, input[12], &reply), JW_RESULT_OK);
  ASSERT_EQ(reply.payload_len, JW_REPLY_HEADER_LEN);
  EXPECT_EQ(reply.payload[0], JW_STATUS_UNSUPPORTED);
  EXPECT_EQ(reply.payload[1], JW_EXECUTION_REJECTED);
  EXPECT_FLOAT_EQ(channel.target_value, 0);

  EXPECT_EQ(CoeCommand(JW_CMD_ESTOP, {}, JW_CHANNEL_NONE),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(CoeCommand(kDisable), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  const auto bytes = CoeCommand(JW_CMD_GET_STATUS);
  jw_device_status_t state;
  ASSERT_EQ(bytes.size(), JW_REPLY_HEADER_LEN + JW_DEVICE_STATUS_LEN);
  ASSERT_EQ(jw_decode_device_status(
                bytes.data() + JW_REPLY_HEADER_LEN, bytes.size() - JW_REPLY_HEADER_LEN, &state),
            JW_RESULT_OK);
  EXPECT_EQ(state.state, JW_STATE_UNAVAILABLE);
  EXPECT_EQ(state.reason, JW_REASON_ESTOP);
  EXPECT_TRUE(state.recovery & JW_RECOVERY_CLEAR_ESTOP);
  EXPECT_TRUE(channel.estopped);
  EXPECT_FALSE(channel.enabled);

  EXPECT_EQ(CoeCommand(JW_CMD_RECOVER, {}, JW_CHANNEL_NONE),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FALSE(channel.estopped);
  EXPECT_FALSE(channel.configured);
  EXPECT_FALSE(channel.enabled);
  EXPECT_EQ(CoeCommand(kEnable), (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
}

}  // namespace
}  // namespace robot::board
