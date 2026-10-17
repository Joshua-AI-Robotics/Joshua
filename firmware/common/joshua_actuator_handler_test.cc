#include "firmware/common/joshua_actuator_handler.h"

#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;

// A backend with delayed admission and partial failures. Tests go through the
// real runtime/session so callback outcomes are checked against wire replies,
// retry behavior and authoritative GET_STATUS rather than implementation calls.
struct Backend {
  jw_actuator_state_t state{};
  jw_actuator_result_t result = jw_actuator_sync_result(JW_STATUS_OK);
  jw_runtime_function_t* function = nullptr;
  jw_configure_step_dir_t config{};
  jw_set_target_t target{};
  jw_feedback_t sample{};
  int mutations = 0;

  jw_actuator_result_t Apply() {
    ++mutations;
    if (result.status == JW_STATUS_ERROR) {
      state.enabled = false;
      function->state.state = JW_STATE_FAULT;
      function->state.reason = JW_REASON_DEVICE_FAULT;
      function->state.recovery = JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE;
    }
    return result;
  }
  bool Completed() const {
    return result.status == JW_STATUS_OK && result.execution == JW_EXECUTION_COMPLETE;
  }
  static jw_actuator_state_t State(void* context) {
    return static_cast<Backend*>(context)->state;
  }
  static jw_actuator_result_t Configure(void* context, const jw_configure_step_dir_t* value) {
    auto* b = static_cast<Backend*>(context);
    if (b->Completed()) {
      b->config = *value;
      b->state.configured = true;
      b->state.enabled = false;
    }
    return b->Apply();
  }
  static jw_actuator_result_t Target(void* context, const jw_set_target_t* value) {
    auto* b = static_cast<Backend*>(context);
    if (b->Completed()) b->target = *value;
    return b->Apply();
  }
  static jw_actuator_result_t Enable(void* context) {
    auto* b = static_cast<Backend*>(context);
    if (b->Completed()) b->state.enabled = true;
    return b->Apply();
  }
  static jw_actuator_result_t Disable(void* context) {
    auto* b = static_cast<Backend*>(context);
    if (b->Completed()) b->state.enabled = false;
    return b->Apply();
  }
  static jw_actuator_result_t Feedback(void* context, jw_feedback_t* value) {
    auto* b = static_cast<Backend*>(context);
    *value = b->sample;
    return b->result;
  }
  static void Safety(void* context, int action) {
    auto* b = static_cast<Backend*>(context);
    b->state.configured = b->state.enabled = false;
    if (action == 1) b->state.estopped = true;
    if (action == 2) b->state.estopped = false;
  }
};
const jw_actuator_backend_ops_t kOps = {Backend::State,
                                        Backend::Configure,
                                        Backend::Target,
                                        Backend::Enable,
                                        Backend::Disable,
                                        Backend::Feedback};

class ActuatorHandlerTest : public ::testing::Test {
 protected:
  Backend backend;
  jw_runtime_function_t function{};
  jw_device_runtime_t runtime{};
  jw_actuator_handler_t handler{};
  jw_endpoint_t endpoint{};
  uint32_t session = 100;
  uint32_t id = 0;
  Bytes last_request;

  void SetUp() override {
    function.descriptor = {200, 17, JW_PROFILE_ACTUATOR, 1, 0, 31, 0};
    handler = {&function,
               {JW_DRIVE_STEP_DIR,
                (1u << JW_MODE_POSITION) | (1u << JW_MODE_VELOCITY),
                JW_UNIT_NATIVE,
                JW_UNIT_NATIVE,
                JW_UNIT_NATIVE,
                JW_CLOCK_BOOT_MONOTONIC_US,
                100000},
               &kOps,
               &backend};
    function.handler = jw_actuator_handle;
    function.context = &handler;
    backend.function = &function;
    backend.sample.metadata = {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 7, 42, 1};
    runtime.identity.function_count = 1;
    runtime.functions = &function;
    runtime.safety = Backend::Safety;
    runtime.context = &backend;
    jw_endpoint_init(&endpoint);
    EXPECT_EQ(Send(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE),
              (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  }
  Bytes Process(const Bytes& request) {
    uint8_t out[JW_MAX_FRAME_LEN];
    const int len = jw_endpoint_process(&endpoint,
                                        request.data(),
                                        request.size(),
                                        out,
                                        sizeof(out),
                                        jw_runtime_command,
                                        jw_runtime_reset,
                                        &runtime);
    if (len <= 0) return {};
    jw_frame_t frame;
    EXPECT_EQ(jw_decode_frame(out, len, &frame), JW_RESULT_OK);
    return Bytes(frame.payload, frame.payload + frame.payload_len);
  }
  Bytes Send(uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
    last_request.resize(JW_MAX_FRAME_LEN);
    const int len = jw_encode_frame(last_request.data(),
                                    last_request.size(),
                                    session,
                                    ++id,
                                    cmd,
                                    channel,
                                    payload.data(),
                                    payload.size());
    EXPECT_GT(len, 0);
    last_request.resize(len);
    return Process(last_request);
  }
  Bytes Profile(uint8_t operation, const Bytes& data = {}) {
    const jw_profile_request_t request{
        JW_PROFILE_ACTUATOR, 1, 0, operation, data.data(), data.size()};
    uint8_t payload[JW_MAX_PAYLOAD_LEN];
    const int len = jw_encode_profile_request(payload, sizeof(payload), &request);
    EXPECT_GT(len, 0);
    return Send(JW_CMD_PROFILE, 200, Bytes(payload, payload + len));
  }
  Bytes Config() {
    const jw_configure_step_dir_t config{1000, 0, 1, 2, 3, 4, 5};
    uint8_t data[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
    const int len = jw_encode_configure_step_dir_payload(data, sizeof(data), &config);
    return Bytes(data, data + len);
  }
  Bytes Target(jw_mode_t mode, float value) {
    uint8_t data[JW_SET_TARGET_PAYLOAD_LEN];
    const int len = jw_encode_set_target_payload(data, sizeof(data), mode, value);
    return Bytes(data, data + len);
  }
};

TEST_F(ActuatorHandlerTest, TypedOperationsAndFeedbackUseRegisteredFunction) {
  ASSERT_EQ(Profile(JW_ACTUATOR_CONFIGURE, Config()), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(backend.config.step_pin, 2);
  EXPECT_EQ(backend.config.step_pulse_width_us, 5);
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(function.state.state, JW_STATE_ACTIVE);
  EXPECT_EQ(Profile(JW_ACTUATOR_SET_TARGET, Target(JW_MODE_POSITION, 12.5f)),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FLOAT_EQ(backend.target.value, 12.5f);
  const auto epoch = function.state.epoch;
  const auto action = function.state.action_message_id;
  backend.sample.position = 9.5f;
  const auto bytes = Profile(JW_ACTUATOR_GET_FEEDBACK);
  jw_feedback_t sample;
  ASSERT_EQ(bytes.size(), JW_REPLY_HEADER_LEN + JW_FEEDBACK_RESPONSE_PAYLOAD_LEN);
  ASSERT_EQ(jw_decode_feedback_payload(
                bytes.data() + JW_REPLY_HEADER_LEN, bytes.size() - JW_REPLY_HEADER_LEN, &sample),
            JW_RESULT_OK);
  EXPECT_FLOAT_EQ(sample.position, 9.5f);
  EXPECT_EQ(sample.metadata.timestamp_us, 42);
  EXPECT_EQ(function.state.epoch, epoch);
  EXPECT_EQ(function.state.action_message_id, action);
  EXPECT_EQ(Profile(JW_ACTUATOR_DISABLE), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FALSE(backend.state.enabled);
  EXPECT_EQ(function.state.state, JW_STATE_DISABLED);
}

TEST_F(ActuatorHandlerTest, AdmissionRejectsBeforeBackendEffects) {
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(Profile(JW_ACTUATOR_CONFIGURE, {0}),
            (Bytes{JW_STATUS_INVALID_PAYLOAD, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(backend.mutations, 0);
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  const int calls = backend.mutations;
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE, {0}),
            (Bytes{JW_STATUS_INVALID_PAYLOAD, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(Profile(JW_ACTUATOR_SET_TARGET, Target(JW_MODE_TORQUE, 1)),
            (Bytes{JW_STATUS_UNSUPPORTED, JW_EXECUTION_REJECTED}));
  const Bytes invalid = {JW_MODE_POSITION, 0, 0, 0x80, 0x7f};  // +infinity, independent bytes.
  EXPECT_EQ(Profile(JW_ACTUATOR_SET_TARGET, invalid),
            (Bytes{JW_STATUS_INVALID_PAYLOAD, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(backend.mutations, calls);
  Profile(JW_ACTUATOR_ENABLE);
  const auto epoch = function.state.epoch;
  EXPECT_EQ(Profile(JW_ACTUATOR_CONFIGURE, Config()),
            (Bytes{JW_STATUS_BUSY, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(function.state.epoch, epoch);
}

TEST_F(ActuatorHandlerTest, ResourceRejectionPreservesConfigurationAndWireDetail) {
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  const auto epoch = function.state.epoch;
  backend.result = jw_actuator_sync_result(JW_STATUS_RESOURCE_CONFLICT);
  backend.result.conflict = {7, JW_RESOURCE_TIMER, 0x1234};
  EXPECT_EQ(
      Profile(JW_ACTUATOR_CONFIGURE, Config()),
      (Bytes{
          JW_STATUS_RESOURCE_CONFLICT, JW_EXECUTION_REJECTED, 7, JW_RESOURCE_TIMER, 0x34, 0x12}));
  EXPECT_TRUE(backend.state.configured);
  EXPECT_EQ(function.state.epoch, epoch);
}

TEST_F(ActuatorHandlerTest, AcceptedWorkRemainsPendingUntilBackendPublishesCompletion) {
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  backend.result.execution = JW_EXECUTION_ACCEPTED;
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), (Bytes{JW_STATUS_OK, JW_EXECUTION_ACCEPTED}));
  const auto action = id;
  EXPECT_FALSE(backend.state.enabled);
  EXPECT_EQ(function.state.state, JW_STATE_DISABLED);
  EXPECT_EQ(function.state.action_execution, JW_EXECUTION_ACCEPTED);
  backend.state.enabled = true;  // Background worker publishes completion.
  function.state.state = JW_STATE_ACTIVE;
  function.state.action_execution = JW_EXECUTION_COMPLETE;
  ++function.state.epoch;
  const auto bytes = Send(JW_CMD_GET_STATUS, 200);
  jw_device_status_t state;
  ASSERT_EQ(jw_decode_device_status(
                bytes.data() + JW_REPLY_HEADER_LEN, bytes.size() - JW_REPLY_HEADER_LEN, &state),
            JW_RESULT_OK);
  EXPECT_EQ(state.action_message_id, action);
  EXPECT_EQ(state.action_execution, JW_EXECUTION_COMPLETE);
  EXPECT_EQ(state.state, JW_STATE_ACTIVE);
}

TEST_F(ActuatorHandlerTest, ConfirmedFailurePreservesAuthoritativeFaultState) {
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  backend.result = jw_actuator_sync_result(JW_STATUS_ERROR);
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), (Bytes{JW_STATUS_ERROR, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(function.state.state, JW_STATE_FAULT);
  EXPECT_EQ(function.state.reason, JW_REASON_DEVICE_FAULT);
  EXPECT_EQ(function.state.action_status, JW_STATUS_ERROR);
  EXPECT_EQ(function.state.action_message_id, id);
  EXPECT_FALSE(backend.state.enabled);
  backend.result = jw_actuator_sync_result(JW_STATUS_OK);
  EXPECT_EQ(Profile(JW_ACTUATOR_DISABLE), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(function.state.state, JW_STATE_FAULT);
  EXPECT_EQ(function.state.reason, JW_REASON_DEVICE_FAULT);
  EXPECT_TRUE(function.state.recovery & JW_RECOVERY_RECONFIGURE);
}

TEST_F(ActuatorHandlerTest, UnknownOutcomeAndMalformedCallbackAreCachedWithoutReexecution) {
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  backend.result = jw_actuator_sync_result(JW_STATUS_OUTCOME_UNKNOWN);
  const auto unknown = Profile(JW_ACTUATOR_ENABLE);
  const auto request = last_request;
  const int calls = backend.mutations;
  EXPECT_EQ(unknown, (Bytes{JW_STATUS_OUTCOME_UNKNOWN, JW_EXECUTION_UNKNOWN}));
  EXPECT_EQ(function.state.action_execution, JW_EXECUTION_UNKNOWN);
  backend.result = jw_actuator_sync_result(JW_STATUS_OK);
  EXPECT_EQ(Process(request), unknown);
  EXPECT_EQ(backend.mutations, calls);
  backend.result.execution = JW_EXECUTION_REJECTED;  // Invalid OK/REJECTED.
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), unknown);
  const auto malformed = last_request;
  EXPECT_EQ(Process(malformed), unknown);
  EXPECT_EQ(backend.mutations, calls + 1);
}

TEST_F(ActuatorHandlerTest, NotReadyFeedbackHasNoSampleOrMutation) {
  backend.result = jw_actuator_sync_result(JW_STATUS_NOT_READY);
  EXPECT_EQ(Profile(JW_ACTUATOR_GET_FEEDBACK), (Bytes{JW_STATUS_NOT_READY, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(backend.mutations, 0);
}

TEST_F(ActuatorHandlerTest, AdvertisedComplexModeReachesBackendWithoutLosingFields) {
  backend.state.configured = true;
  handler.schema.target_modes = 1u << JW_MODE_COMPLEX;
  const jw_complex_target_t target{1, 2, 3};
  uint8_t data[JW_SET_TARGET_COMPLEX_PAYLOAD_LEN];
  const int len = jw_encode_complex_target_payload(data, sizeof(data), &target);
  ASSERT_GT(len, 0);
  EXPECT_EQ(Profile(JW_ACTUATOR_SET_TARGET, Bytes(data, data + len)),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(backend.target.mode, JW_MODE_COMPLEX);
  EXPECT_FLOAT_EQ(backend.target.combined.position, 1);
  EXPECT_FLOAT_EQ(backend.target.combined.velocity, 2);
  EXPECT_FLOAT_EQ(backend.target.combined.torque, 3);
}

TEST_F(ActuatorHandlerTest, ResetAndDisablePreserveEstopUntilExplicitRecovery) {
  Profile(JW_ACTUATOR_CONFIGURE, Config());
  Profile(JW_ACTUATOR_ENABLE);
  Send(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0});
  EXPECT_EQ(Profile(JW_ACTUATOR_DISABLE), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(function.state.state, JW_STATE_UNAVAILABLE);
  EXPECT_TRUE(function.state.recovery & JW_RECOVERY_CLEAR_ESTOP);
  ++session;
  id = 0;
  Send(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE);
  const int calls = backend.mutations;
  EXPECT_EQ(Profile(JW_ACTUATOR_CONFIGURE, Config()),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(backend.mutations, calls);
  Send(JW_CMD_RECOVER, JW_CHANNEL_NONE, {0, 0});
  EXPECT_FALSE(backend.state.enabled);
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE), (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
}

TEST_F(ActuatorHandlerTest, InsufficientReplyCapacityCannotApplyConfiguration) {
  const auto data = Config();
  const jw_profile_request_t request{
      JW_PROFILE_ACTUATOR, 1, 0, JW_ACTUATOR_CONFIGURE, data.data(), data.size()};
  const jw_command_t command{100, 2, JW_CMD_PROFILE, 200, nullptr, 0};
  uint8_t out[JW_REPLY_HEADER_LEN];
  EXPECT_EQ(jw_actuator_handle(&handler, &command, &request, out, sizeof(out)), -1);
  EXPECT_EQ(backend.mutations, 0);
}

TEST_F(ActuatorHandlerTest, NonStepDirBackendDoesNotAcceptStepDirConfiguration) {
  handler.schema.drive = JW_DRIVE_PWM_DC;
  EXPECT_EQ(Profile(JW_ACTUATOR_CONFIGURE, Config()),
            (Bytes{JW_STATUS_UNSUPPORTED, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(backend.mutations, 0);
}
}  // namespace
