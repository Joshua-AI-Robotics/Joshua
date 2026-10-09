// Executable architecture example. No device I/O, sleeps, or production drivers.
#include <algorithm>
#include <array>
#include <vector>

#include "firmware/common/joshua_wire_commands.h"
#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/reference/joshua_wire_device/device_contract.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kMotor = 0x10001;
constexpr uint32_t kTemperature = 0x20001;
constexpr uint32_t kIo = 0x30001;
constexpr uint32_t kDiagnostics = 0x40001;
constexpr uint32_t kUnknownProfile = 0xfeed0001;

uint32_t Get32(const uint8_t* p) {
  return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

Bytes Invoke(uint32_t profile, uint8_t operation, const Bytes& data = {}) {
  Bytes result{uint8_t(profile),
               uint8_t(profile >> 8),
               uint8_t(profile >> 16),
               uint8_t(profile >> 24),
               1,
               0,
               operation};
  result.insert(result.end(), data.begin(), data.end());
  return result;
}

Bytes Frame(
    uint32_t session, uint32_t id, uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
  Bytes result(JW_MAX_FRAME_LEN);
  const int length = jw_encode_frame(result.data(),
                                     result.size(),
                                     session,
                                     id,
                                     cmd,
                                     channel,
                                     payload.data(),
                                     static_cast<uint8_t>(payload.size()));
  EXPECT_GT(length, 0);
  result.resize(length > 0 ? length : 0);
  return result;
}

// The table is test wiring, not a second source of production robot configuration.
struct Board {
  jw_endpoint_t endpoint{};
  std::array<jwd_descriptor_t, 11> descriptors{};
  std::array<jwd_state_t, 10> states{};
  std::array<bool, 10> configured{};
  std::array<int, 10> pins{};
  uint32_t generation = 0;
  uint32_t boot_id = 42;
  int writes = 0;
  int resets = 0;
  bool sample_ready = false;

  Board() {
    pins.fill(-1);
    for (uint8_t channel = 0; channel < states.size(); ++channel) {
      const uint32_t profile = IsMotor(channel) ? kMotor : channel == 3 ? kIo : kTemperature;
      descriptors[channel] = {
          channel,
          uint16_t(channel < 2 ? 100 : 100 + channel),
          profile,
          1,
          JWD_OP_READ | JWD_OP_CONFIGURE | (IsMotor(channel) ? JWD_OP_WRITE : 0u),
          uint16_t(channel < 2 ? 1 : 2)};
      states[channel].channel = channel;
      states[channel].safety_group = descriptors[channel].safety_group;
    }
    // The motor channel has an additional optional profile, not an exclusive kind.
    descriptors[10] = {0, 100, kDiagnostics, 1, JWD_OP_READ, 1};
    jw_endpoint_init(&endpoint);
  }

  static bool IsMotor(uint8_t channel) {
    return channel == 0 || channel == 2;
  }

  static void Reset(void* context) {
    auto& board = *static_cast<Board*>(context);
    ++board.resets;
    ++board.generation;
    board.configured.fill(false);
    board.pins.fill(-1);
    board.sample_ready = false;
    for (auto& state : board.states) {
      state.state = JWD_STATE_DISABLED;
      state.reason = JWD_REASON_RESET;
      state.stop_evidence = IsMotor(state.channel) ? JWD_STOP_OUTPUT_DISABLED : JWD_STOP_UNKNOWN;
      state.recovery = JWD_RECOVERY_CONFIGURE | (IsMotor(state.channel) ? JWD_RECOVERY_ENABLE : 0);
      state.generation = board.generation;
    }
  }

  void Stop() {
    ++generation;
    sample_ready = false;
    for (auto& state : states) {
      state.state = IsMotor(state.channel) ? JWD_STATE_DISABLED : JWD_STATE_UNAVAILABLE;
      state.reason = JWD_REASON_ESTOP;
      state.stop_evidence = IsMotor(state.channel) ? JWD_STOP_OUTPUT_DISABLED : JWD_STOP_UNKNOWN;
      state.recovery = JWD_RECOVERY_CONFIGURE | (IsMotor(state.channel) ? JWD_RECOVERY_ENABLE : 0);
      state.generation = generation;
    }
    configured.fill(false);
    pins.fill(-1);
  }

  void SensorFault(uint8_t channel) {
    states[channel].state = JWD_STATE_UNAVAILABLE;
    states[channel].reason = JWD_REASON_FAULT;
    states[channel].recovery = JWD_RECOVERY_CONFIGURE;
    configured[channel] = false;
    sample_ready = false;
    ++generation;
  }

  static int Handle(void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
    auto& board = *static_cast<Board*>(context);
    if (cap < JW_MAX_PAYLOAD_LEN) return -1;
    const auto reply = [&](uint8_t status) {
      out[0] = status;
      return 1;
    };
    const bool board_scope = command->channel == JW_CHANNEL_NONE;
    if (command->cmd == JW_CMD_ESTOP) {
      if (!board_scope || command->payload_len != 0) return reply(JW_STATUS_ERROR);
      board.Stop();
      return reply(JW_STATUS_OK);  // Legacy response, not an extension envelope.
    }
    if (command->cmd == JWD_INFO) {
      if (!board_scope || command->payload_len != 0) return reply(JWD_INVALID_REQUEST);
      const jwd_info_t info{JWD_CONTRACT_VERSION,
                            JW_PROTO_VERSION,
                            JW_MAX_PAYLOAD_LEN,
                            1,
                            uint16_t(board.descriptors.size()),
                            123,
                            456,
                            1,
                            board.boot_id};
      out[0] = JWD_OK;
      return 1 + jwd_encode_info(out + 1, cap - 1, &info);
    }
    if (command->cmd == JWD_DESCRIBE) {
      if (!board_scope || command->payload_len != 2) return reply(JWD_INVALID_REQUEST);
      const unsigned index = command->payload[0] | (unsigned(command->payload[1]) << 8);
      if (index >= board.descriptors.size()) return reply(JWD_INVALID_REQUEST);
      out[0] = JWD_OK;
      return 1 + jwd_encode_descriptor(out + 1, cap - 1, &board.descriptors[index]);
    }
    if (command->channel >= board.states.size()) return reply(JWD_INVALID_REQUEST);
    auto& state = board.states[command->channel];
    if (command->cmd == JWD_STATUS) {
      if (command->payload_len != 0) return reply(JWD_INVALID_REQUEST);
      out[0] = JWD_OK;
      auto snapshot = state;
      snapshot.generation = board.generation;
      return 1 + jwd_encode_state(out + 1, cap - 1, &snapshot);
    }
    if (command->cmd == JW_CMD_ENABLE) {
      if (command->payload_len != 0 || !IsMotor(command->channel) ||
          !board.configured[command->channel])
        return reply(JW_STATUS_ERROR);
      state.state = JWD_STATE_READY;
      state.reason = JWD_REASON_NONE;
      state.stop_evidence = JWD_STOP_UNKNOWN;
      state.recovery = 0;
      ++board.generation;
      return reply(JW_STATUS_OK);
    }
    if (command->cmd != JWD_INVOKE) return reply(JWD_UNSUPPORTED);
    if (command->payload_len < JWD_INVOKE_HEADER_SIZE) return reply(JWD_INVALID_REQUEST);
    const uint32_t profile = Get32(command->payload);
    const uint16_t version = command->payload[4] | (uint16_t(command->payload[5]) << 8);
    const auto found =
        std::find_if(board.descriptors.begin(), board.descriptors.end(), [&](const auto& d) {
          return d.channel == command->channel && d.profile == profile &&
                 d.profile_version == version;
        });
    if (found == board.descriptors.end()) return reply(JWD_UNSUPPORTED);
    const uint8_t op = command->payload[6];
    if ((op != JWD_OP_READ && op != JWD_OP_WRITE && op != JWD_OP_CONFIGURE) ||
        (found->operations & op) == 0)
      return reply(JWD_UNSUPPORTED);
    const size_t data_length = command->payload_len - JWD_INVOKE_HEADER_SIZE;
    if (data_length != (op == JWD_OP_READ ? 0u : 1u)) return reply(JWD_INVALID_REQUEST);
    if (op == JWD_OP_CONFIGURE) {
      const int pin = command->payload[JWD_INVOKE_HEADER_SIZE];
      for (size_t other = 0; other < board.pins.size(); ++other) {
        if (other != command->channel && board.pins[other] == pin)
          return reply(JWD_RESOURCE_CONFLICT);
      }
      // Commit only after all validation; a rejected reconfiguration changes nothing.
      board.pins[command->channel] = pin;
      board.configured[command->channel] = true;
      state.state = IsMotor(command->channel) ? JWD_STATE_DISABLED : JWD_STATE_READY;
      state.reason = JWD_REASON_NONE;
      state.stop_evidence = IsMotor(command->channel) ? JWD_STOP_OUTPUT_DISABLED : JWD_STOP_UNKNOWN;
      state.recovery = IsMotor(command->channel) ? JWD_RECOVERY_ENABLE : 0;
      ++board.generation;
      return reply(JWD_OK);
    }
    if (!board.configured[command->channel] || state.state != JWD_STATE_READY)
      return reply(JWD_UNAVAILABLE);
    if (op == JWD_OP_WRITE) {
      ++board.writes;
      return reply(JWD_OK);
    }
    if (profile == kTemperature) {
      // Acquisition is external to dispatch. Never wait for conversion here.
      if (!board.sample_ready) return reply(JWD_NOT_READY);
      out[0] = JWD_OK;
      out[1] = 25;  // Example profile: one unsigned byte in degrees C.
      return 2;
    }
    // Other example READ profiles expose a one-byte diagnostic/input value.
    out[0] = JWD_OK;
    out[1] = 0;
    return 2;
  }

  Bytes Process(const Bytes& request) {
    Bytes response(JW_MAX_FRAME_LEN);
    const int length = jw_endpoint_process(&endpoint,
                                           request.data(),
                                           request.size(),
                                           response.data(),
                                           response.size(),
                                           Handle,
                                           Reset,
                                           this);
    EXPECT_GE(length, 0);
    response.resize(length > 0 ? length : 0);
    return response;
  }
};

// One owner allocates IDs and completes each call before the next driver runs.
class MixedDevice : public ::testing::Test {
 protected:
  Board board;
  uint32_t session = 7;
  uint32_t next_id = 1;

  Bytes Call(uint8_t cmd, uint8_t channel, const Bytes& body = {}) {
    const auto request = Frame(session, next_id++, cmd, channel, body);
    const auto response = board.Process(request);
    jw_frame_t sent{}, received{};
    EXPECT_EQ(jw_decode_frame(request.data(), request.size(), &sent), JW_RESULT_OK);
    if (jw_decode_frame(response.data(), response.size(), &received) != JW_RESULT_OK) {
      ADD_FAILURE() << "Missing response";
      return {};
    }
    EXPECT_EQ(jw_check_response(&sent, &received), JW_MATCH_OK);
    return Bytes(received.payload, received.payload + received.payload_len);
  }
  void SetUp() override {
    ASSERT_EQ(Call(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE), Bytes{JW_STATUS_OK});
  }
  void Configure(uint8_t channel, uint32_t profile, uint8_t pin) {
    EXPECT_EQ(Call(JWD_INVOKE, channel, Invoke(profile, JWD_OP_CONFIGURE, {pin})), Bytes{JWD_OK});
  }
  void EnableMotor() {
    Configure(0, kMotor, 5);
    ASSERT_EQ(Call(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_OK});
  }
  jwd_state_t State(uint8_t channel) {
    const auto body = Call(JWD_STATUS, channel);
    jwd_state_t state{};
    if (body.size() != 1 + JWD_STATE_SIZE || body[0] != JWD_OK) {
      ADD_FAILURE() << "Invalid status response";
      return state;
    }
    EXPECT_EQ(jwd_decode_state(body.data() + 1, body.size() - 1, &state), JW_RESULT_OK);
    return state;
  }
};

TEST_F(MixedDevice, DiscoversTenFunctionsAndMultipleProfilesWithinFrameLimit) {
  const auto body = Call(JWD_INFO, JW_CHANNEL_NONE);
  ASSERT_EQ(body.size(), 1 + JWD_INFO_SIZE);
  jwd_info_t info{};
  ASSERT_EQ(jwd_decode_info(body.data() + 1, body.size() - 1, &info), JW_RESULT_OK);
  EXPECT_EQ(info.boot_id, 42);
  EXPECT_EQ(info.descriptor_count, 11);
  EXPECT_LE(body.size(), JW_MAX_PAYLOAD_LEN);
  for (uint16_t ordinal = 0; ordinal < info.descriptor_count; ++ordinal) {
    const auto entry =
        Call(JWD_DESCRIBE, JW_CHANNEL_NONE, {uint8_t(ordinal), uint8_t(ordinal >> 8)});
    ASSERT_EQ(entry.size(), 1 + JWD_DESCRIPTOR_SIZE);
    jwd_descriptor_t descriptor{};
    ASSERT_EQ(jwd_decode_descriptor(entry.data() + 1, entry.size() - 1, &descriptor), JW_RESULT_OK);
    EXPECT_EQ(descriptor.channel, ordinal < 10 ? ordinal : 0);
    if (ordinal == 10) {
      EXPECT_EQ(descriptor.profile, kDiagnostics);
      EXPECT_EQ(descriptor.physical_device, 100);
    }
  }
  EXPECT_EQ(Call(JWD_DESCRIBE, JW_CHANNEL_NONE, {11, 0}), Bytes{JWD_INVALID_REQUEST});
}

TEST_F(MixedDevice, SlowAndFailedSensorsDoNotPreventMotorCommandsOrEstop) {
  EnableMotor();
  Configure(1, kTemperature, 6);
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), Bytes{JWD_NOT_READY});
  EXPECT_EQ(Call(JWD_INVOKE, 0, Invoke(kMotor, JWD_OP_WRITE, {10})), Bytes{JWD_OK});
  board.sample_ready = true;  // Acquisition task completes independently.
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), (Bytes{JWD_OK, 25}));
  board.SensorFault(1);
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), Bytes{JWD_UNAVAILABLE});
  EXPECT_EQ(State(1).reason, JWD_REASON_FAULT);
  EXPECT_EQ(State(0).state, JWD_STATE_READY);
  EXPECT_EQ(Call(JW_CMD_ESTOP, JW_CHANNEL_NONE), Bytes{JW_STATUS_OK});
  EXPECT_EQ(board.writes, 1);
  const auto motor = State(0);
  const auto sensor = State(1);
  EXPECT_EQ(motor.state, JWD_STATE_DISABLED);
  EXPECT_EQ(motor.stop_evidence, JWD_STOP_OUTPUT_DISABLED);
  EXPECT_NE(motor.stop_evidence, JWD_STOP_PHYSICALLY_VERIFIED);
  EXPECT_EQ(sensor.state, JWD_STATE_UNAVAILABLE);
  EXPECT_EQ(sensor.reason, JWD_REASON_ESTOP);
  EXPECT_EQ(sensor.safety_group, motor.safety_group);
  EXPECT_EQ(sensor.generation, motor.generation);
  Configure(1, kTemperature, 6);
  EXPECT_EQ(State(0).state, JWD_STATE_DISABLED);
  EXPECT_EQ(Call(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_ERROR});
}

TEST_F(MixedDevice, TwoMotorsSensorsAndIoShareTheSameSession) {
  EnableMotor();
  Configure(2, kMotor, 7);
  Configure(1, kTemperature, 6);
  Configure(3, kIo, 8);
  ASSERT_EQ(Call(JW_CMD_ENABLE, 2), Bytes{JW_STATUS_OK});
  EXPECT_EQ(Call(JWD_INVOKE, 0, Invoke(kMotor, JWD_OP_WRITE, {10})), Bytes{JWD_OK});
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), Bytes{JWD_NOT_READY});
  EXPECT_EQ(Call(JWD_INVOKE, 2, Invoke(kMotor, JWD_OP_WRITE, {20})), Bytes{JWD_OK});
  EXPECT_EQ(Call(JWD_INVOKE, 3, Invoke(kIo, JWD_OP_READ)), (Bytes{JWD_OK, 0}));
  EXPECT_EQ(Call(JWD_INVOKE, 0, Invoke(kDiagnostics, JWD_OP_READ)), (Bytes{JWD_OK, 0}));
  EXPECT_EQ(board.writes, 2);
  EXPECT_EQ(board.resets, 1);
  EXPECT_EQ(Call(JW_CMD_ESTOP, JW_CHANNEL_NONE), Bytes{JW_STATUS_OK});
  EXPECT_EQ(State(0).state, JWD_STATE_DISABLED);
  EXPECT_EQ(State(2).state, JWD_STATE_DISABLED);
  EXPECT_EQ(State(3).reason, JWD_REASON_ESTOP);
}

TEST_F(MixedDevice, StatusGenerationDetectsChangesBetweenQueries) {
  EnableMotor();
  const auto before = State(0);
  EXPECT_EQ(Call(JW_CMD_ESTOP, JW_CHANNEL_NONE), Bytes{JW_STATUS_OK});
  const auto after = State(1);
  EXPECT_NE(before.generation, after.generation);
  EXPECT_EQ(State(0).generation, after.generation);
}

TEST_F(MixedDevice, ConflictAndMalformedRequestLeaveExistingConfigurationIntact) {
  EnableMotor();
  Configure(1, kTemperature, 6);
  const auto before = board.generation;
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_CONFIGURE, {5})),
            Bytes{JWD_RESOURCE_CONFLICT});
  EXPECT_EQ(board.pins[1], 6);
  EXPECT_EQ(board.generation, before);
  EXPECT_EQ(State(0).state, JWD_STATE_READY);
  EXPECT_EQ(Call(JWD_INVOKE, 0, Invoke(kMotor, JWD_OP_WRITE, {10, 11})),
            Bytes{JWD_INVALID_REQUEST});
  EXPECT_EQ(Call(JWD_INVOKE, 0, Invoke(kUnknownProfile, JWD_OP_WRITE, {10})),
            Bytes{JWD_UNSUPPORTED});
  EXPECT_EQ(Call(JWD_STATUS, 99), Bytes{JWD_INVALID_REQUEST});
  auto wrong_version = Invoke(kMotor, JWD_OP_WRITE, {10});
  wrong_version[4] = 2;
  EXPECT_EQ(Call(JWD_INVOKE, 0, wrong_version), Bytes{JWD_UNSUPPORTED});
  EXPECT_EQ(board.writes, 0);
}

TEST_F(MixedDevice, RetriesNeverRepeatAnActuationEvenAfterInterveningSensorTraffic) {
  EnableMotor();
  Configure(1, kTemperature, 6);
  const auto target = Frame(session, next_id++, JWD_INVOKE, 0, Invoke(kMotor, JWD_OP_WRITE, {10}));
  const auto original = board.Process(target);
  ASSERT_FALSE(original.empty());
  EXPECT_EQ(board.Process(target), original);  // Retry after losing the response.
  EXPECT_EQ(board.writes, 1);
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), Bytes{JWD_NOT_READY});
  EXPECT_TRUE(board.Process(target).empty());  // Older retry: outcome remains unknown to caller.
  EXPECT_EQ(board.writes, 1);
}

TEST_F(MixedDevice, IndependentDriverOrderingIsRejectedAndSharedResetInvalidatesAllChannels) {
  EnableMotor();
  Configure(1, kTemperature, 6);
  const auto delayed = Frame(session, next_id++, JWD_INVOKE, 0, Invoke(kMotor, JWD_OP_WRITE, {10}));
  EXPECT_EQ(Call(JWD_INVOKE, 1, Invoke(kTemperature, JWD_OP_READ)), Bytes{JWD_NOT_READY});
  EXPECT_TRUE(board.Process(delayed).empty());  // Demonstrates why a common scheduler is required.
  EXPECT_EQ(board.writes, 0);
  board.sample_ready = true;
  ++session;
  next_id = 1;
  EXPECT_EQ(Call(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE), Bytes{JW_STATUS_OK});
  EXPECT_EQ(board.resets, 2);
  EXPECT_EQ(State(0).reason, JWD_REASON_RESET);
  EXPECT_EQ(State(1).reason, JWD_REASON_RESET);
  EXPECT_FALSE(board.sample_ready);
  EXPECT_FALSE(board.configured[0]);
  EXPECT_FALSE(board.configured[1]);
  EXPECT_EQ(Call(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_ERROR});
  EXPECT_TRUE(board.Process(delayed).empty());
}

TEST(DeviceContract, DescriptorUsesExplicitLittleEndianBytesAndPreservesUnknownProfiles) {
  const jwd_descriptor_t descriptor{9, 0x1234, kUnknownProfile, 0x0201, 0x80000001, 0x5678};
  std::array<uint8_t, JWD_DESCRIPTOR_SIZE> bytes{};
  ASSERT_EQ(jwd_encode_descriptor(bytes.data(), bytes.size(), &descriptor), bytes.size());
  EXPECT_EQ(Bytes(bytes.begin(), bytes.end()),
            (Bytes{9, 0x34, 0x12, 1, 0, 0xed, 0xfe, 1, 2, 1, 0, 0, 0x80, 0x78, 0x56}));
  jwd_descriptor_t decoded{};
  ASSERT_EQ(jwd_decode_descriptor(bytes.data(), bytes.size(), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.profile, kUnknownProfile);
  EXPECT_EQ(decoded.operations, 0x80000001);
}

TEST(DeviceContract, InfoAndStateMatchIndependentGoldenBytes) {
  const jwd_info_t info{1, 2, 49, 1, 11, 0x12345678, 0xaabbccdd, 7, 42};
  Bytes bytes(JWD_INFO_SIZE);
  ASSERT_EQ(jwd_encode_info(bytes.data(), bytes.size(), &info), bytes.size());
  EXPECT_EQ(bytes, (Bytes{1,    2,    49,   1, 11, 0, 0x78, 0x56, 0x34, 0x12, 0xdd,
                          0xcc, 0xbb, 0xaa, 7, 0,  0, 0,    42,   0,    0,    0}));
  jwd_info_t decoded_info{};
  ASSERT_EQ(jwd_decode_info(bytes.data(), bytes.size(), &decoded_info), JW_RESULT_OK);
  EXPECT_EQ(decoded_info.vendor_id, 0x12345678);
  EXPECT_EQ(decoded_info.product_id, 0xaabbccdd);
  EXPECT_EQ(decoded_info.firmware_revision, 7);
  EXPECT_EQ(decoded_info.boot_id, 42);
  const jwd_state_t state{1,
                          JWD_STATE_UNAVAILABLE,
                          JWD_REASON_ESTOP,
                          JWD_STOP_UNKNOWN,
                          JWD_RECOVERY_CONFIGURE,
                          0x12345678,
                          0xabcd};
  bytes.resize(JWD_STATE_SIZE);
  ASSERT_EQ(jwd_encode_state(bytes.data(), bytes.size(), &state), bytes.size());
  EXPECT_EQ(bytes, (Bytes{1, 3, 2, 0, 1, 0x78, 0x56, 0x34, 0x12, 0xcd, 0xab}));
  jwd_state_t decoded_state{};
  ASSERT_EQ(jwd_decode_state(bytes.data(), bytes.size(), &decoded_state), JW_RESULT_OK);
  EXPECT_EQ(decoded_state.generation, 0x12345678);
  EXPECT_EQ(decoded_state.safety_group, 0xabcd);
}

// Check every truncation without reading/writing beyond the supplied buffer.
template <typename T, typename Encode, typename Decode>
void CheckBounds(size_t size, Encode encode, Decode decode) {
  T value{};
  Bytes bytes(size + 1, 0xa5);
  for (size_t n = 0; n < size; ++n) {
    EXPECT_EQ(encode(bytes.data(), n, &value), -1);
    EXPECT_EQ(bytes, Bytes(size + 1, 0xa5));
    EXPECT_EQ(decode(bytes.data(), n, &value), JW_RESULT_ERROR);
  }
  EXPECT_EQ(encode(nullptr, size, &value), -1);
  EXPECT_EQ(encode(bytes.data(), size, nullptr), -1);
  EXPECT_EQ(decode(nullptr, size, &value), JW_RESULT_ERROR);
  EXPECT_EQ(decode(bytes.data(), size, nullptr), JW_RESULT_ERROR);
  EXPECT_EQ(decode(bytes.data(), size + 1, &value), JW_RESULT_ERROR);
  EXPECT_EQ(encode(bytes.data(), size, &value), size);
  EXPECT_EQ(bytes.back(), 0xa5);
  EXPECT_EQ(decode(bytes.data(), size, &value), JW_RESULT_OK);
}

TEST(DeviceContract, RejectsTruncatedAndOversizedBodiesAndNullArguments) {
  CheckBounds<jwd_info_t>(JWD_INFO_SIZE, jwd_encode_info, jwd_decode_info);
  CheckBounds<jwd_descriptor_t>(JWD_DESCRIPTOR_SIZE, jwd_encode_descriptor, jwd_decode_descriptor);
  CheckBounds<jwd_state_t>(JWD_STATE_SIZE, jwd_encode_state, jwd_decode_state);
}
}  // namespace
