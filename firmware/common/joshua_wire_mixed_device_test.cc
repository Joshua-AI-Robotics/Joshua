// Hardware-free executable protocol fixture, NOT a production board/scheduler.
// Fake extension profiles: 0x8001 sensor READ(0)/pin CONFIGURE(1, u8);
// 0x8002 PWM CONFIGURE(0, pin:u8 + frequency:u32le); 0x8000 diagnostics READ(0).
#include <algorithm>
#include <array>
#include <deque>
#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/joshua_wire_resources.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;
uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
Bytes Cursor(uint32_t generation, uint16_t cursor) {
  return {uint8_t(generation),
          uint8_t(generation >> 8),
          uint8_t(generation >> 16),
          uint8_t(generation >> 24),
          uint8_t(cursor),
          uint8_t(cursor >> 8)};
}
Bytes Frame(
    uint32_t session, uint32_t id, uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
  Bytes frame(JW_MAX_FRAME_LEN);
  const int n = jw_encode_frame(
      frame.data(), frame.size(), session, id, cmd, channel, payload.data(), payload.size());
  EXPECT_GT(n, 0);
  frame.resize(n > 0 ? n : 0);
  return frame;
}
Bytes Profile(uint16_t id, uint8_t op, const Bytes& data = {}) {
  Bytes payload(JW_MAX_PAYLOAD_LEN);
  const jw_profile_request_t r{id, 1, 0, op, data.data(), data.size()};
  const int n = jw_encode_profile_request(payload.data(), payload.size(), &r);
  EXPECT_GT(n, 0);
  payload.resize(n > 0 ? n : 0);
  return payload;
}
jw_reply_t Reply(const Bytes& response) {
  jw_frame_t frame{};
  jw_reply_t reply{};
  EXPECT_EQ(jw_decode_frame(response.data(), response.size(), &frame), JW_RESULT_OK);
  EXPECT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
  return reply;
}
Bytes StepConfig(uint8_t step = 2, uint8_t dir = 3, uint8_t enable = 4) {
  jw_configure_step_dir_t config{1000, 0, 1, step, dir, enable, 20};
  Bytes bytes(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
  EXPECT_EQ(jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config),
            bytes.size());
  return bytes;
}
Bytes Target(float value) {
  Bytes bytes(JW_SET_TARGET_PAYLOAD_LEN);
  EXPECT_EQ(jw_encode_set_target_payload(bytes.data(), bytes.size(), JW_MODE_POSITION, value),
            bytes.size());
  return bytes;
}

struct MixedFirmware {
  static constexpr size_t kFunctions = 12;
  jw_endpoint_t endpoint{};  // Both transports MUST use this SAME endpoint.
  jw_identify_response_t identity{};
  std::array<jw_function_descriptor_t, kFunctions> functions{};
  std::array<jw_device_status_t, kFunctions> states{};
  jw_device_status_t board{};
  std::array<bool, kFunctions> configured{};
  std::array<jw_sensor_sample_t, kFunctions> samples{};
  std::array<bool, kFunctions> ready{}, failed{}, converting{};
  std::array<jw_resource_claim_t, 32> resources{};
  int resets = 0, calls = 0, targets = 0, acquisitions = 0;
  int dropped_replies = 0;
  Bytes injected_reply;
  bool link = true, estop = false, pending_stop = false, fail_after_action = false;
  float position = 0;
  uint64_t now = 0;
  std::vector<uint8_t> call_order;
  MixedFirmware() {
    identity.vendor_id = 0xfeed;
    identity.product_id = 0x12340001;  // No core enum.
    identity.boot_id = 7;
    identity.discovery_generation = 1;
    identity.function_count = kFunctions;
    identity.max_frame_len = JW_MAX_FRAME_LEN;
    identity.max_inflight = 1;
    identity.max_page_entries = JW_DISCOVERY_PAGE_SIZE;
    identity.max_handler_us = 50;
    identity.safety_flags = JW_SAFETY_BOARD_ESTOP | JW_SAFETY_COMM_INTERRUPTED;
    identity.protocol_min = identity.protocol_max = JW_PROTO_VERSION;
    identity.supported_planes = JW_PLANE_MESSAGE;
    for (uint8_t i = 0; i < kFunctions; ++i)
      functions[i] = {i, uint16_t(i < 4 ? 100 : 100 + i), JW_PROFILE_SENSOR, 1, 0, 1, 0};
    functions[0].profile_id = JW_PROFILE_ACTUATOR;
    functions[0].operations = 0x1f;
    functions[3].profile_id = 0x8000;
    functions[4].profile_id = functions[6].profile_id = 0x8002;
    functions[5].profile_id = 0x8001;
    functions[5].operations = 3;
    jw_resource_table_init(resources.data(), resources.size());
  }
  static void Reset(void* context) {
    auto& f = *static_cast<MixedFirmware*>(context);
    ++f.resets;
    if (f.pending_stop) f.CompleteStop();
    f.configured.fill(false);
    f.ready.fill(false);
    f.converting.fill(false);
    jw_resource_table_init(f.resources.data(), f.resources.size());
    f.board.state = f.estop ? JW_STATE_UNAVAILABLE : JW_STATE_READY;
    f.board.reason = f.estop ? JW_REASON_ESTOP : JW_REASON_SESSION_RESET;
    ++f.board.epoch;
    for (uint8_t i = 0; i < kFunctions; ++i) {
      f.states[i].state = f.estop ? JW_STATE_UNAVAILABLE : JW_STATE_DISABLED;
      f.states[i].reason = f.estop ? JW_REASON_SHUTDOWN_DEPENDENCY : JW_REASON_SESSION_RESET;
      f.states[i].recovery = (i == 0 ? JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE : 0) |
                             (f.estop ? JW_RECOVERY_CLEAR_ESTOP : 0);
      ++f.states[i].epoch;
    }
  }
  int Error(uint8_t* out, size_t cap, jw_status_t status) {
    return jw_encode_reply_payload(out, cap, status, JW_EXECUTION_REJECTED, nullptr, 0);
  }
  int Data(uint8_t* out,
           size_t cap,
           const uint8_t* data,
           size_t len,
           jw_execution_t phase = JW_EXECUTION_COMPLETE) {
    return jw_encode_reply_payload(out, cap, JW_STATUS_OK, phase, data, len);
  }
  void Change(uint8_t id,
              jw_device_state_t state,
              jw_state_reason_t reason,
              const jw_command_t& c) {
    auto& s = id == JW_CHANNEL_NONE ? board : states[id];
    s.state = state;
    s.reason = reason;
    ++s.epoch;
    s.action_session_id = c.session_id;
    s.action_message_id = c.message_id;
    s.action_execution = JW_EXECUTION_COMPLETE;
    s.action_status = JW_STATUS_OK;
  }
  static int Handle(void* context, const jw_command_t* c, uint8_t* out, size_t cap) {
    return static_cast<MixedFirmware*>(context)->Dispatch(*c, out, cap);
  }
  int Dispatch(const jw_command_t& c, uint8_t* out, size_t cap) {
    ++calls;
    call_order.push_back(c.channel);
    uint8_t data[JW_MAX_PAYLOAD_LEN]{};
    int n = 0;
    if (c.channel != JW_CHANNEL_NONE && c.channel >= kFunctions)
      return Error(out, cap, JW_STATUS_INVALID_CHANNEL);
    switch (c.cmd) {
      case JW_CMD_IDENTIFY:
        if (c.payload_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        n = jw_encode_identify_payload(data, sizeof(data), &identity);
        return Data(out, cap, data, n);
      case JW_CMD_DISCOVER: {
        if (c.payload_len != 6) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        uint32_t generation = U32(c.payload);
        uint16_t cursor = c.payload[4] | uint16_t(c.payload[5]) << 8;
        if (generation != identity.discovery_generation && !(generation == 0 && cursor == 0))
          return Error(out, cap, JW_STATUS_STALE_DISCOVERY);
        if (cursor >= kFunctions) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        jw_discovery_page_t page{};
        page.generation = identity.discovery_generation;
        page.total = kFunctions;
        page.count = std::min<size_t>(JW_DISCOVERY_PAGE_SIZE, kFunctions - cursor);
        page.next_cursor = cursor + page.count == kFunctions ? JW_CURSOR_END : cursor + page.count;
        for (uint8_t i = 0; i < page.count; ++i) page.functions[i] = functions[cursor + i];
        n = jw_encode_discovery_page(data, sizeof(data), &page);
        return Data(out, cap, data, n);
      }
      case JW_CMD_DESCRIBE_FUNCTION: {
        if (c.payload_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        n = jw_encode_function_descriptor(data, sizeof(data), &functions[c.channel]);
        if (c.channel == 0) {
          const jw_actuator_schema_t schema{JW_DRIVE_STEP_DIR,
                                            1u << JW_MODE_POSITION,
                                            JW_UNIT_NATIVE,
                                            JW_UNIT_NATIVE,
                                            JW_UNIT_NATIVE,
                                            JW_CLOCK_BOOT_MONOTONIC_US,
                                            1000};
          n += jw_encode_actuator_schema(data + n, sizeof(data) - n, &schema);
        } else if (functions[c.channel].profile_id == JW_PROFILE_SENSOR) {
          const jw_sensor_schema_t schema{
              JW_SENSOR_INTERFACE_SCALAR_F32,
              c.channel == 2 ? uint16_t(JW_UNIT_CELSIUS) : uint16_t(JW_UNIT_RADIAN),
              JW_CLOCK_BOOT_MONOTONIC_US,
              1000};
          n += jw_encode_sensor_schema(data + n, sizeof(data) - n, &schema);
        }
        return Data(out, cap, data, n);
      }
      case JW_CMD_GET_DEPENDENCIES: {
        if (c.payload_len != 6 || c.payload[4] != 0 || c.payload[5] != 0)
          return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        if (U32(c.payload) != identity.discovery_generation)
          return Error(out, cap, JW_STATUS_STALE_DISCOVERY);
        jw_dependency_page_t page{};
        page.generation = identity.discovery_generation;
        page.next_cursor = JW_CURSOR_END;
        page.count = kFunctions;
        for (uint8_t i = 0; i < kFunctions; ++i)
          page.dependencies[i] = {0, i, uint8_t(i == 0 ? JW_SHUTDOWN_OUTPUT : JW_SHUTDOWN_POWER)};
        n = jw_encode_dependency_page(data, sizeof(data), &page);
        return Data(out, cap, data, n);
      }
      case JW_CMD_GET_STATUS:
        if (c.payload_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        n = jw_encode_device_status(
            data, sizeof(data), c.channel == JW_CHANNEL_NONE ? &board : &states[c.channel]);
        return Data(out, cap, data, n);
      case JW_CMD_ESTOP: {
        if (c.payload_len != 2 || c.payload[0] != 0 || c.payload[1] != 0)
          return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        estop = true;
        pending_stop = true;
        Change(JW_CHANNEL_NONE, JW_STATE_UNAVAILABLE, JW_REASON_ESTOP, c);
        board.stop_phase = JW_STOP_ACCEPTED;
        board.action_execution = JW_EXECUTION_ACCEPTED;
        board.verification = JW_SAFE_UNSUPPORTED;
        board.recovery = JW_RECOVERY_CLEAR_ESTOP;
        for (uint8_t i = 0; i < kFunctions; ++i) {
          Change(i,
                 i == 0 ? JW_STATE_DISABLED : JW_STATE_UNAVAILABLE,
                 i == 0 ? JW_REASON_ESTOP : JW_REASON_SHUTDOWN_DEPENDENCY,
                 c);
          states[i].stop_phase = JW_STOP_ACCEPTED;
          states[i].action_execution = JW_EXECUTION_ACCEPTED;
          states[i].verification = JW_SAFE_UNSUPPORTED;
          states[i].recovery = JW_RECOVERY_CLEAR_ESTOP |
                               (i == 0 ? JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE : 0);
        }
        configured.fill(false);
        ready.fill(false);
        converting.fill(false);
        return Data(out, cap, nullptr, 0, JW_EXECUTION_ACCEPTED);
      }
      case JW_CMD_RECOVER:
        if (c.payload_len != 2 || c.payload[0] != 0 || c.payload[1] != 0)
          return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        if (pending_stop) return Error(out, cap, JW_STATUS_BUSY);
        estop = false;
        Change(JW_CHANNEL_NONE, JW_STATE_READY, JW_REASON_NONE, c);
        board.recovery = 0;
        board.stop_phase = JW_STOP_NONE;
        for (uint8_t i = 0; i < kFunctions; ++i) {
          Change(i, i == 0 ? JW_STATE_DISABLED : JW_STATE_READY, JW_REASON_NONE, c);
          states[i].recovery = i == 0 ? JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE : 0;
          states[i].stop_phase = JW_STOP_NONE;
        }
        jw_resource_table_init(resources.data(), resources.size());
        return Data(out, cap, nullptr, 0);
      case JW_CMD_PROFILE:
        break;
      default:
        return Error(out, cap, JW_STATUS_UNSUPPORTED);
    }
    jw_profile_request_t r{};
    if (jw_decode_profile_request(c.payload, c.payload_len, &r) != JW_RESULT_OK)
      return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
    const auto& f = functions[c.channel];
    if (r.profile_id != f.profile_id || r.major != f.profile_major || r.minor > f.profile_minor)
      return Error(out, cap, JW_STATUS_PROFILE_MISMATCH);
    if (!jw_function_supports_operation(&f, &r)) return Error(out, cap, JW_STATUS_UNSUPPORTED);
    if (estop) return Error(out, cap, JW_STATUS_UNAVAILABLE);
    if (r.profile_id == JW_PROFILE_ACTUATOR) {
      if (r.operation == JW_ACTUATOR_CONFIGURE) {
        if (states[c.channel].state == JW_STATE_ACTIVE) return Error(out, cap, JW_STATUS_BUSY);
        jw_configure_step_dir_t config{};
        if (jw_decode_configure_step_dir_payload(r.data, r.data_len, &config) != JW_RESULT_OK)
          return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        const jw_resource_claim_t claims[] = {
            {c.channel, JW_RESOURCE_PIN, config.step_pin, 0, 0},
            {c.channel, JW_RESOURCE_PIN, config.dir_pin, 0, 0},
            {c.channel, JW_RESOURCE_PIN, config.enable_pin, 0, 0}};
        const auto status = Configure(c.channel, claims, 3, out, cap);
        if (status != JW_STATUS_OK) return conflict_reply;
        configured[c.channel] = true;
        states[c.channel].recovery = JW_RECOVERY_EXPLICIT_ENABLE;
        Change(c.channel, JW_STATE_DISABLED, JW_REASON_NONE, c);
        return Data(out, cap, nullptr, 0);
      }
      if (r.operation == JW_ACTUATOR_ENABLE) {
        if (r.data_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        if (!configured[c.channel]) return Error(out, cap, JW_STATUS_UNAVAILABLE);
        states[c.channel].recovery = 0;
        Change(c.channel, JW_STATE_ACTIVE, JW_REASON_NONE, c);
        return Data(out, cap, nullptr, 0);
      }
      if (r.operation == JW_ACTUATOR_DISABLE) {
        if (r.data_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        Change(c.channel, JW_STATE_DISABLED, JW_REASON_NONE, c);
        states[c.channel].recovery = JW_RECOVERY_EXPLICIT_ENABLE;
        return Data(out, cap, nullptr, 0);
      }
      if (r.operation == JW_ACTUATOR_SET_TARGET) {
        jw_set_target_t target{};
        if (jw_decode_set_target_payload(r.data, r.data_len, &target) != JW_RESULT_OK ||
            target.mode != JW_MODE_POSITION)
          return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        if (states[c.channel].state != JW_STATE_ACTIVE)
          return Error(out, cap, JW_STATUS_UNAVAILABLE);
        position = target.value;
        ++targets;
        Change(c.channel, JW_STATE_ACTIVE, JW_REASON_NONE, c);
        if (fail_after_action) return -1;  // Side effect happened; outcome cannot be confirmed.
        return Data(out, cap, nullptr, 0);
      }
      if (r.operation == JW_ACTUATOR_GET_FEEDBACK) {
        if (r.data_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
        const jw_feedback_t feedback{position,
                                     0,
                                     0,
                                     {JW_SAMPLE_VALID,
                                      JW_CLOCK_BOOT_MONOTONIC_US,
                                      identity.boot_id,
                                      now,
                                      uint32_t(targets)}};
        n = jw_encode_feedback_payload(data, sizeof(data), &feedback);
        return Data(out, cap, data, n);
      }
    }
    if (r.profile_id == 0x8002) {  // Fake PWM configuration extension, shared timer 2.
      if (r.data_len != 5) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
      const jw_resource_claim_t claims[] = {{c.channel, JW_RESOURCE_PIN, r.data[0], 0, 0},
                                            {c.channel, JW_RESOURCE_TIMER, 2, 1, U32(r.data + 1)}};
      if (Configure(c.channel, claims, 2, out, cap) != JW_STATUS_OK) return conflict_reply;
      configured[c.channel] = true;
      Change(c.channel, JW_STATE_DISABLED, JW_REASON_NONE, c);
      return Data(out, cap, nullptr, 0);
    }
    if (r.profile_id == 0x8001 && r.operation == 1) {  // Fake sensor pin configuration.
      if (r.data_len != 1) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
      const jw_resource_claim_t claim{c.channel, JW_RESOURCE_PIN, r.data[0], 0, 0};
      if (Configure(c.channel, &claim, 1, out, cap) != JW_STATUS_OK) return conflict_reply;
      configured[c.channel] = true;
      return Data(out, cap, nullptr, 0);
    }
    if (r.profile_id == 0x8000) {  // Unknown to core, supported by this fake backend.
      if (r.data_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
      data[0] = 0xaa;
      data[1] = 0x55;
      return Data(out, cap, data, 2);
    }
    if (r.data_len) return Error(out, cap, JW_STATUS_INVALID_PAYLOAD);
    if (failed[c.channel])
      return jw_encode_reply_payload(out, cap, JW_STATUS_ERROR, JW_EXECUTION_COMPLETE, nullptr, 0);
    if (!ready[c.channel]) {
      converting[c.channel] = true;
      return Error(out, cap, JW_STATUS_NOT_READY);
    }
    auto sample = samples[c.channel];
    if (now - sample.metadata.timestamp_us > 1000) sample.metadata.validity = JW_SAMPLE_STALE;
    n = jw_encode_sensor_sample_payload(
        data, sizeof(data), JW_SENSOR_INTERFACE_SCALAR_F32, &sample);
    return Data(out, cap, data, n);
  }
  int conflict_reply = 0;
  jw_status_t Configure(
      uint8_t id, const jw_resource_claim_t* claims, size_t count, uint8_t* out, size_t cap) {
    jw_resource_conflict_t conflict{};
    const auto status =
        jw_resource_replace(resources.data(), resources.size(), id, claims, count, &conflict);
    const uint8_t data[] = {
        conflict.owner, conflict.kind, uint8_t(conflict.id), uint8_t(conflict.id >> 8)};
    if (status != JW_STATUS_OK)
      conflict_reply =
          jw_encode_reply_payload(out,
                                  cap,
                                  status,
                                  JW_EXECUTION_REJECTED,
                                  status == JW_STATUS_RESOURCE_CONFLICT ? data : nullptr,
                                  status == JW_STATUS_RESOURCE_CONFLICT ? 4 : 0);
    return status;
  }
  // Background work, never called by Dispatch. Advance simulated conversion time.
  void Acquire(uint8_t id, float value) {
    if (estop) return;
    ++acquisitions;
    now += 2000;
    ready[id] = true;
    converting[id] = false;
    samples[id] = {value,
                   {JW_SAMPLE_VALID,
                    JW_CLOCK_BOOT_MONOTONIC_US,
                    identity.boot_id,
                    now,
                    uint32_t(acquisitions)}};
    states[id].state = JW_STATE_READY;
  }
  void FailAcquisition(uint8_t id) {
    failed[id] = true;
    converting[id] = false;
    states[id].state = JW_STATE_FAULT;
    states[id].reason = JW_REASON_DEVICE_FAULT;
    ++states[id].epoch;
  }
  void CompleteStop() {
    pending_stop = false;
    board.stop_phase = JW_STOP_ACTION_COMPLETE;
    board.action_execution = JW_EXECUTION_COMPLETE;
    ++board.epoch;
    for (auto& s : states) {
      s.stop_phase = JW_STOP_ACTION_COMPLETE;
      s.action_execution = JW_EXECUTION_COMPLETE;
      ++s.epoch;
    }
    // No physical feedback exists in this fixture; never report SAFE_VERIFIED.
  }
  Bytes Exchange(const Bytes& request, int transport = 0) {
    EXPECT_TRUE(transport == 0 || transport == 1);  // Serial and mailbox fake adapters.
    if (!link) return {};
    if (!injected_reply.empty()) {
      auto reply = injected_reply;
      injected_reply.clear();
      return reply;
    }
    Bytes response(JW_MAX_FRAME_LEN);
    const int n = jw_endpoint_process(&endpoint,
                                      request.data(),
                                      request.size(),
                                      response.data(),
                                      response.size(),
                                      Handle,
                                      Reset,
                                      this);
    if (n <= 0) return {};
    response.resize(n);
    if (dropped_replies > 0) {
      --dropped_replies;
      return {};
    }
    return response;
  }
};

// One fake host coordinator for ALL device handles/transports. A timeout keeps
// the exact request pending and gates every other normal caller until retry or
// reconnect+reconciliation. Production synchronization/scheduling is stack work.
struct Host {
  MixedFirmware& firmware;
  uint32_t session = 0, id = 0;
  Bytes pending;
  bool known = false;
  explicit Host(MixedFirmware& f) : firmware(f) {}
  Bytes Send(uint8_t cmd,
             uint8_t channel = JW_CHANNEL_NONE,
             const Bytes& payload = {},
             int transport = 0) {
    if (!pending.empty()) return {};  // Enforce endpoint-wide single flight.
    pending = Frame(session, ++id, cmd, channel, payload);
    return Retry(transport);
  }
  Bytes Retry(int transport = 0) {
    if (pending.empty()) return {};
    auto reply = firmware.Exchange(pending, transport);
    if (reply.empty()) {
      known = false;
      return {};
    }
    jw_frame_t sent{}, received{};
    EXPECT_EQ(jw_decode_frame(pending.data(), pending.size(), &sent), JW_RESULT_OK);
    EXPECT_EQ(jw_decode_frame(reply.data(), reply.size(), &received), JW_RESULT_OK);
    if (!jw_response_matches(&sent, &received)) {
      known = false;
      return {};
    }
    if (Reply(reply).status == JW_STATUS_OUTCOME_UNKNOWN) known = false;
    pending.clear();
    return reply;
  }
  Bytes Call(uint8_t function, uint8_t op, const Bytes& data = {}, int transport = 0) {
    return Send(JW_CMD_PROFILE,
                function,
                Profile(firmware.functions[function].profile_id, op, data),
                transport);
  }
  bool Connect(uint32_t next, int transport = 0) {
    known = false;
    pending.clear();
    session = next;
    id = 0;
    const auto response = Send(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE, {}, transport);
    if (response.empty()) return false;
    // Mandatory queries reconcile actual state, NOT the reset acknowledgment.
    if (Send(JW_CMD_IDENTIFY).empty() || Send(JW_CMD_GET_STATUS).empty()) return false;
    for (uint8_t i = 0; i < MixedFirmware::kFunctions; ++i)
      if (Send(JW_CMD_GET_STATUS, i).empty()) return false;
    known = true;
    return true;
  }
  jw_device_status_t State(uint8_t channel = JW_CHANNEL_NONE) {
    const auto response = Send(JW_CMD_GET_STATUS, channel);
    const auto reply = Reply(response);
    jw_device_status_t state{};
    EXPECT_EQ(jw_decode_device_status(reply.data, reply.data_len, &state), JW_RESULT_OK);
    return state;
  }
};

// Bounded queue fixture: separate sensor/control capacities and a reserved stop
// slot; IDs are allocated by Host only when Pump publishes a selected request.
struct FixtureScheduler {
  struct Work {
    uint8_t cmd;
    uint8_t function;
    Bytes payload;
  };
  Host& host;
  std::deque<Work> controls, sensors, safety;
  explicit FixtureScheduler(Host& h) : host(h) {}
  bool Enqueue(uint8_t function, uint8_t operation, const Bytes& data, bool control) {
    auto& queue = control ? controls : sensors;
    if (queue.size() == 2) return false;
    queue.push_back({JW_CMD_PROFILE,
                     function,
                     Profile(host.firmware.functions[function].profile_id, operation, data)});
    return true;
  }
  bool Stop() {
    if (!safety.empty()) return false;
    safety.push_back({JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0}});
    return true;
  }
  Bytes Pump() {
    if (!host.pending.empty() && safety.empty()) return {};
    if (!safety.empty()) {
      // Retire the unresolved normal ID before publishing a higher-ID stop.
      // Its old reply/action outcome can no longer be treated as known.
      host.pending.clear();
      host.known = false;
      auto work = safety.front();
      safety.pop_front();
      controls.clear();
      sensors.clear();
      return host.Send(work.cmd, work.function, work.payload);
    }
    auto& queue = controls.empty() ? sensors : controls;
    if (queue.empty()) return {};
    auto work = queue.front();
    queue.pop_front();
    return host.Send(work.cmd, work.function, work.payload);
  }
};

class MixedDeviceTest : public ::testing::Test {
 protected:
  MixedFirmware firmware;
  Host host{firmware};
  void SetUp() override {
    ASSERT_TRUE(host.Connect(100));
  }
  void Enable() {
    EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_CONFIGURE, StepConfig())).status, JW_STATUS_OK);
    EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_ENABLE)).status, JW_STATUS_OK);
  }
};

TEST_F(MixedDeviceTest, DiscoversMoreThanEightMultifunctionProfilesAndUnknownBoard) {
  const auto response = host.Send(JW_CMD_IDENTIFY);
  const auto reply = Reply(response);
  jw_identify_response_t identity{};
  ASSERT_EQ(jw_decode_identify_payload(reply.data, reply.data_len, &identity), JW_RESULT_OK);
  EXPECT_EQ(identity.function_count, 12);
  EXPECT_EQ(identity.product_id, 0x12340001);
  uint16_t cursor = 0;
  uint32_t generation = 0;
  std::vector<jw_function_descriptor_t> found;
  do {
    const auto r = host.Send(JW_CMD_DISCOVER, JW_CHANNEL_NONE, Cursor(generation, cursor));
    const auto payload = Reply(r);
    jw_discovery_page_t page{};
    ASSERT_EQ(jw_decode_discovery_page(payload.data, payload.data_len, &page), JW_RESULT_OK);
    generation = page.generation;
    cursor = page.next_cursor;
    found.insert(found.end(), page.functions, page.functions + page.count);
  } while (cursor != JW_CURSOR_END);
  ASSERT_EQ(found.size(), 12);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(found[i].device_id, 100);
  EXPECT_EQ(found[0].profile_id, JW_PROFILE_ACTUATOR);
  EXPECT_EQ(found[1].profile_id, JW_PROFILE_SENSOR);
  EXPECT_EQ(found[3].profile_id, 0x8000);
  const auto description = host.Send(JW_CMD_DESCRIBE_FUNCTION, 2);
  const auto d = Reply(description);
  jw_sensor_schema_t schema{};
  ASSERT_EQ(
      jw_decode_sensor_schema(
          d.data + JW_FUNCTION_DESCRIPTOR_LEN, d.data_len - JW_FUNCTION_DESCRIPTOR_LEN, &schema),
      JW_RESULT_OK);
  EXPECT_EQ(schema.unit, JW_UNIT_CELSIUS);
  const auto diag = host.Call(3, 0);
  EXPECT_EQ(Reply(diag).data_len, 2);
  ++firmware.identity.discovery_generation;
  EXPECT_EQ(Reply(host.Send(JW_CMD_DISCOVER, JW_CHANNEL_NONE, Cursor(generation, 4))).status,
            JW_STATUS_STALE_DISCOVERY);
}
TEST_F(MixedDeviceTest, InterleavesCommandsSlowFailedAndStaleSensorsWithoutBlocking) {
  Enable();
  const auto before = firmware.now;
  EXPECT_EQ(Reply(host.Call(1, JW_SENSOR_READ)).status, JW_STATUS_NOT_READY);
  EXPECT_TRUE(firmware.converting[1]);
  EXPECT_EQ(firmware.acquisitions, 0);
  EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_SET_TARGET, Target(10))).status, JW_STATUS_OK);
  EXPECT_EQ(firmware.now, before);  // Dispatch never advances/waits for conversion.
  firmware.FailAcquisition(2);
  EXPECT_EQ(Reply(host.Call(2, JW_SENSOR_READ)).status, JW_STATUS_ERROR);
  EXPECT_EQ(host.State(2).reason, JW_REASON_DEVICE_FAULT);
  EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_SET_TARGET, Target(20))).status, JW_STATUS_OK);
  firmware.Acquire(1, 0.75f);
  const auto sensor = host.Call(1, JW_SENSOR_READ);
  auto r = Reply(sensor);
  jw_sensor_sample_t sample{};
  ASSERT_EQ(
      jw_decode_sensor_sample_payload(r.data, r.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
      JW_RESULT_OK);
  EXPECT_EQ(sample.metadata.clock, JW_CLOCK_BOOT_MONOTONIC_US);
  EXPECT_EQ(sample.metadata.validity, JW_SAMPLE_VALID);
  const auto seq = sample.metadata.sequence;
  const auto time = sample.metadata.timestamp_us;
  firmware.now += 1001;
  const auto stale = host.Call(1, JW_SENSOR_READ);
  r = Reply(stale);
  ASSERT_EQ(
      jw_decode_sensor_sample_payload(r.data, r.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
      JW_RESULT_OK);
  EXPECT_EQ(sample.metadata.validity, JW_SAMPLE_STALE);
  EXPECT_EQ(sample.metadata.sequence, seq);
  EXPECT_EQ(sample.metadata.timestamp_us, time);
  EXPECT_EQ(firmware.targets, 2);
}
TEST_F(MixedDeviceTest, LostActuatorAndSensorResponsesRetryAcrossTransportsExactlyOnce) {
  Enable();
  firmware.dropped_replies = 1;
  EXPECT_TRUE(host.Call(0, JW_ACTUATOR_SET_TARGET, Target(30), 0).empty());
  EXPECT_FALSE(host.known);
  EXPECT_EQ(firmware.targets, 1);
  const auto pending = host.pending;
  EXPECT_TRUE(host.Call(1, JW_SENSOR_READ, {}, 1).empty());
  EXPECT_EQ(host.pending, pending);
  EXPECT_EQ(Reply(host.Retry(1)).status, JW_STATUS_OK);
  EXPECT_EQ(firmware.targets, 1);
  firmware.Acquire(1, 3);
  firmware.dropped_replies = 1;
  EXPECT_TRUE(host.Call(1, JW_SENSOR_READ, {}, 1).empty());
  const auto calls = firmware.calls;
  EXPECT_EQ(Reply(host.Retry(0)).status, JW_STATUS_OK);
  EXPECT_EQ(firmware.calls, calls);
  // Changed payload at same ID cannot trigger an action, including on another plane.
  EXPECT_TRUE(firmware
                  .Exchange(Frame(host.session,
                                  host.id,
                                  JW_CMD_PROFILE,
                                  0,
                                  Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_SET_TARGET, Target(99))),
                            1)
                  .empty());
  EXPECT_EQ(firmware.targets, 1);
}
TEST_F(MixedDeviceTest, HandlerFailureIsUnknownAndCannotRepeatSideEffects) {
  Enable();
  firmware.fail_after_action = true;
  const auto response = host.Call(0, JW_ACTUATOR_SET_TARGET, Target(40));
  EXPECT_EQ(Reply(response).status, JW_STATUS_OUTCOME_UNKNOWN);
  EXPECT_EQ(Reply(response).execution, JW_EXECUTION_UNKNOWN);
  const auto replay =
      firmware.Exchange(Frame(host.session,
                              host.id,
                              JW_CMD_PROFILE,
                              0,
                              Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_SET_TARGET, Target(40))),
                        1);
  EXPECT_EQ(replay, response);
  EXPECT_EQ(firmware.targets, 1);
  const auto status = host.State(0);
  EXPECT_EQ(status.action_message_id, host.id - 1);
  EXPECT_EQ(firmware.position, 40);
}
TEST_F(MixedDeviceTest, ConflictingPinsAndSharedTimerChangesAreRejectedAtomically) {
  Enable();
  const auto initial = firmware.resources;
  const auto response = host.Call(5, 1, {2});
  const auto conflict = Reply(response);
  EXPECT_EQ(conflict.status, JW_STATUS_RESOURCE_CONFLICT);
  ASSERT_EQ(conflict.data_len, 4);
  EXPECT_EQ(conflict.data[0], 0);
  EXPECT_EQ(conflict.data[1], JW_RESOURCE_PIN);
  EXPECT_EQ(conflict.data[2], 2);
  EXPECT_EQ(memcmp(initial.data(), firmware.resources.data(), sizeof(initial)), 0);
  EXPECT_EQ(firmware.states[0].state, JW_STATE_ACTIVE);
  EXPECT_FALSE(firmware.configured[5]);
  EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_CONFIGURE, StepConfig(8, 9, 10))).status,
            JW_STATUS_BUSY);
  EXPECT_EQ(Reply(host.Call(4, 0, {12, 0xe8, 3, 0, 0})).status, JW_STATUS_OK);
  EXPECT_EQ(Reply(host.Call(6, 0, {13, 0xe8, 3, 0, 0})).status, JW_STATUS_OK);
  const auto shared = firmware.resources;
  EXPECT_EQ(Reply(host.Call(4, 0, {12, 0xd0, 7, 0, 0})).status, JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(memcmp(shared.data(), firmware.resources.data(), sizeof(shared)), 0);
}
TEST_F(MixedDeviceTest, SharedEstopIsObservableAndRecoveryNeverEnablesActuators) {
  Enable();
  const auto deps = host.Send(JW_CMD_GET_DEPENDENCIES, JW_CHANNEL_NONE, Cursor(1, 0));
  const auto d = Reply(deps);
  jw_dependency_page_t dependencies{};
  ASSERT_EQ(jw_decode_dependency_page(d.data, d.data_len, &dependencies), JW_RESULT_OK);
  EXPECT_EQ(dependencies.count, 12);
  EXPECT_EQ(dependencies.dependencies[2].effects, JW_SHUTDOWN_POWER);
  const auto stop = host.Send(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0});
  EXPECT_EQ(Reply(stop).execution, JW_EXECUTION_ACCEPTED);
  EXPECT_EQ(host.State().stop_phase, JW_STOP_ACCEPTED);
  EXPECT_EQ(host.State().verification, JW_SAFE_UNSUPPORTED);
  EXPECT_EQ(host.State(2).reason, JW_REASON_SHUTDOWN_DEPENDENCY);
  EXPECT_EQ(Reply(host.Call(2, JW_SENSOR_READ)).status, JW_STATUS_UNAVAILABLE);
  EXPECT_EQ(Reply(host.Send(JW_CMD_RECOVER, JW_CHANNEL_NONE, {0, 0})).status, JW_STATUS_BUSY);
  firmware.CompleteStop();
  EXPECT_EQ(host.State().stop_phase, JW_STOP_ACTION_COMPLETE);
  EXPECT_EQ(host.State().verification, JW_SAFE_UNSUPPORTED);
  EXPECT_EQ(Reply(host.Send(JW_CMD_RECOVER, JW_CHANNEL_NONE, {0, 0})).status, JW_STATUS_OK);
  firmware.Acquire(2, 25);
  EXPECT_EQ(Reply(host.Call(2, JW_SENSOR_READ)).status, JW_STATUS_OK);
  const auto actuator = host.State(0);
  EXPECT_EQ(actuator.state, JW_STATE_DISABLED);
  EXPECT_EQ(actuator.recovery, JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE);
  EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_ENABLE)).status, JW_STATUS_UNAVAILABLE);
}
TEST_F(MixedDeviceTest, InterruptedCommunicationAndReconnectReconcileEveryFunction) {
  Enable();
  firmware.dropped_replies = 1;
  EXPECT_TRUE(host.Send(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0}).empty());
  firmware.link = false;
  EXPECT_FALSE(host.known);
  EXPECT_TRUE(host.Retry(1).empty());
  firmware.CompleteStop();
  firmware.link = true;
  const auto old_pending = host.pending;
  const auto resets = firmware.resets;
  ASSERT_TRUE(host.Connect(101, 1));
  EXPECT_EQ(firmware.resets, resets + 1);
  EXPECT_TRUE(firmware.estop);
  EXPECT_EQ(host.State(0).state, JW_STATE_UNAVAILABLE);
  EXPECT_EQ(host.State(2).reason, JW_REASON_SHUTDOWN_DEPENDENCY);
  EXPECT_TRUE(firmware.Exchange(old_pending, 0).empty());
  EXPECT_EQ(Reply(host.Call(0, JW_ACTUATOR_ENABLE)).status, JW_STATUS_UNAVAILABLE);
  const auto boot = firmware.identity.boot_id;
  jw_endpoint_init(&firmware.endpoint);
  ++firmware.identity.boot_id;
  MixedFirmware::Reset(&firmware);
  EXPECT_TRUE(host.Send(JW_CMD_GET_STATUS).empty());
  EXPECT_FALSE(host.known);
  ASSERT_TRUE(host.Connect(102));
  EXPECT_NE(firmware.identity.boot_id, boot);
  EXPECT_EQ(host.State(0).state, JW_STATE_UNAVAILABLE);
}
TEST_F(MixedDeviceTest, RetainedReplyCannotCompleteNewSessionRequest) {
  Enable();
  const auto old_reply = host.Call(0, JW_ACTUATOR_SET_TARGET, Target(50));
  ASSERT_TRUE(host.Connect(200, 1));
  firmware.injected_reply = old_reply;
  EXPECT_TRUE(host.Send(JW_CMD_GET_STATUS, 0).empty());
  EXPECT_FALSE(host.pending.empty());
  EXPECT_FALSE(host.known);
  const auto reply = host.Retry(1);
  EXPECT_TRUE(host.pending.empty());
  const auto r = Reply(reply);
  jw_device_status_t state{};
  ASSERT_EQ(jw_decode_device_status(r.data, r.data_len, &state), JW_RESULT_OK);
  EXPECT_EQ(state.state, JW_STATE_DISABLED);
  EXPECT_EQ(state.reason, JW_REASON_SESSION_RESET);
  EXPECT_EQ(firmware.targets, 1);
}
TEST_F(MixedDeviceTest, RebootInvalidatesMeasurementsAndBootClockDomain) {
  firmware.Acquire(1, 12);
  const auto old = host.Call(1, JW_SENSOR_READ);
  auto r = Reply(old);
  jw_sensor_sample_t prior{};
  ASSERT_EQ(
      jw_decode_sensor_sample_payload(r.data, r.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &prior),
      JW_RESULT_OK);
  jw_endpoint_init(&firmware.endpoint);
  ++firmware.identity.boot_id;
  firmware.now = 0;
  firmware.acquisitions = 0;
  MixedFirmware::Reset(&firmware);
  ASSERT_TRUE(host.Connect(201));
  EXPECT_EQ(Reply(host.Call(1, JW_SENSOR_READ)).status, JW_STATUS_NOT_READY);
  firmware.Acquire(1, 13);
  const auto current = host.Call(1, JW_SENSOR_READ);
  r = Reply(current);
  jw_sensor_sample_t sample{};
  ASSERT_EQ(
      jw_decode_sensor_sample_payload(r.data, r.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
      JW_RESULT_OK);
  EXPECT_NE(sample.metadata.boot_id, prior.metadata.boot_id);
  EXPECT_EQ(sample.metadata.sequence, 1);
  EXPECT_EQ(sample.metadata.timestamp_us, 2000);
  EXPECT_FALSE(firmware.configured[0]);
  EXPECT_EQ(host.State(0).state, JW_STATE_DISABLED);
}

TEST_F(MixedDeviceTest, InvalidChannelsVersionsOperationsAndPayloadsHaveDistinctErrors) {
  EXPECT_EQ(Reply(host.Send(JW_CMD_PROFILE, 200, Profile(JW_PROFILE_SENSOR, 0))).status,
            JW_STATUS_INVALID_CHANNEL);
  EXPECT_EQ(Reply(host.Send(JW_CMD_PROFILE, 1, Profile(JW_PROFILE_ACTUATOR, 1))).status,
            JW_STATUS_PROFILE_MISMATCH);
  EXPECT_EQ(Reply(host.Send(JW_CMD_PROFILE, 1, Profile(JW_PROFILE_SENSOR, 1))).status,
            JW_STATUS_UNSUPPORTED);
  EXPECT_EQ(Reply(host.Send(JW_CMD_PROFILE, 1, {2, 0, 1, 0})).status, JW_STATUS_INVALID_PAYLOAD);
  EXPECT_EQ(Reply(host.Send(0x03, 0)).status, JW_STATUS_UNSUPPORTED);  // Retired device opcode.
  EXPECT_EQ(Reply(host.Send(JW_CMD_IDENTIFY, 1)).status, JW_STATUS_INVALID_CHANNEL);
}

// A bounded scheduling fixture models the required priority/reservation contract.
// No producer can consume the reserved control queue with sensor requests.
TEST_F(MixedDeviceTest, BoundedQueuesPrioritizeControlAndStopUnderSensorLoad) {
  Enable();
  FixtureScheduler scheduler(host);
  EXPECT_TRUE(scheduler.Enqueue(1, JW_SENSOR_READ, {}, false));
  EXPECT_TRUE(scheduler.Enqueue(2, JW_SENSOR_READ, {}, false));
  EXPECT_FALSE(scheduler.Enqueue(7, JW_SENSOR_READ, {}, false));
  EXPECT_TRUE(scheduler.Enqueue(0, JW_ACTUATOR_SET_TARGET, Target(1), true));
  EXPECT_TRUE(scheduler.Enqueue(0, JW_ACTUATOR_SET_TARGET, Target(2), true));
  EXPECT_FALSE(scheduler.Enqueue(0, JW_ACTUATOR_SET_TARGET, Target(3), true));
  EXPECT_EQ(Reply(scheduler.Pump()).status, JW_STATUS_OK);
  EXPECT_EQ(Reply(scheduler.Pump()).status, JW_STATUS_OK);
  EXPECT_EQ(firmware.targets, 2);
  EXPECT_EQ(firmware.acquisitions, 0);
  EXPECT_EQ(Reply(scheduler.Pump()).status, JW_STATUS_NOT_READY);
  // A response timeout cannot prevent reserved safety admission/publication.
  firmware.dropped_replies = 1;
  EXPECT_TRUE(scheduler.Enqueue(0, JW_ACTUATOR_SET_TARGET, Target(4), true));
  EXPECT_TRUE(scheduler.Pump().empty());
  const auto retired = host.pending;
  EXPECT_FALSE(retired.empty());
  const auto retired_id = host.id;
  EXPECT_TRUE(scheduler.Stop());
  EXPECT_EQ(Reply(scheduler.Pump()).execution, JW_EXECUTION_ACCEPTED);
  EXPECT_GT(host.id, retired_id);
  EXPECT_TRUE(host.pending.empty());
  EXPECT_EQ(firmware.states[0].state, JW_STATE_DISABLED);
  EXPECT_TRUE(firmware.Exchange(retired, 1).empty());  // Retired mutation cannot replay.
  EXPECT_EQ(firmware.targets, 3);
  EXPECT_EQ(firmware.acquisitions, 0);
}
TEST(JoshuaWireResources, InvalidAndCapacityFailuresPreserveOwnership) {
  std::array<jw_resource_claim_t, 2> table{};
  jw_resource_table_init(table.data(), table.size());
  jw_resource_claim_t first{0, JW_RESOURCE_PIN, 2, 0, 0};
  ASSERT_EQ(jw_resource_replace(table.data(), table.size(), 0, &first, 1, nullptr), JW_STATUS_OK);
  const auto before = table;
  jw_resource_claim_t claims[] = {{1, JW_RESOURCE_PIN, 3, 0, 0}, {1, JW_RESOURCE_PIN, 4, 0, 0}};
  EXPECT_EQ(jw_resource_replace(table.data(), table.size(), 1, claims, 2, nullptr), JW_STATUS_BUSY);
  EXPECT_EQ(memcmp(before.data(), table.data(), sizeof(table)), 0);
  claims[1].id = 3;
  EXPECT_EQ(jw_resource_replace(table.data(), table.size(), 1, claims, 2, nullptr),
            JW_STATUS_INVALID_PAYLOAD);
  EXPECT_EQ(memcmp(before.data(), table.data(), sizeof(table)), 0);
  EXPECT_EQ(jw_resource_replace(table.data(), table.size(), 0, nullptr, 0, nullptr), JW_STATUS_OK);
  EXPECT_EQ(table[0].owner, JW_CHANNEL_NONE);
}
}  // namespace
