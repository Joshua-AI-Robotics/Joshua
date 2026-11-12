#include "joshua_stepdir_device.h"

#include <Arduino.h>
#include <limits.h>
#include <string.h>

#include "backend_stepdir.h"

namespace {
jw_endpoint_t endpoint;
jw_device_runtime_t runtime;
uint8_t schema[JW_ACTUATOR_SCHEMA_LEN];
bool (*valid_pin)(uint8_t);
uint32_t previous_micros;
uint64_t timestamp_us;
jw_resource_claim_t* resources;
size_t resource_capacity;
StepDirDeviceExtension extension;
constexpr uint32_t kMaxFeedbackAgeUs = 100000;

void Safety(void*, int reason) {
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    StepDirDisable(&g_channels[i]);
    g_channels[i].configured = false;
    g_channels[i].target_mode = JW_MODE_INVALID;
    g_channels[i].target_value = 0;
    g_channels[i].feedback.metadata.validity = JW_SAMPLE_INVALID;
  }
  jw_resource_table_init(resources, resource_capacity);
  if (extension.safety) extension.safety(extension.context, reason);
}

jw_actuator_state_t State(void* context) {
  const auto* channel = static_cast<ChannelState*>(context);
  return {channel->configured, channel->enabled, runtime.estopped != 0};
}
jw_actuator_result_t Configure(void* context, const jw_configure_step_dir_t* config) {
  auto* channel = static_cast<ChannelState*>(context);
  if (!valid_pin || !valid_pin(config->step_pin) || !valid_pin(config->dir_pin) ||
      !valid_pin(config->enable_pin) || !config->max_pulse_rate_hz ||
      config->max_pulse_rate_hz >
          1000000u / (1u + (config->step_pulse_width_us ? config->step_pulse_width_us
                                                        : JW_STEPDIR_DEFAULT_PULSE_WIDTH_US)))
    return jw_actuator_sync_result(JW_STATUS_INVALID_PAYLOAD);
  const uint8_t id = static_cast<uint8_t>(channel - g_channels);
  const jw_resource_claim_t claims[] = {{id, JW_RESOURCE_PIN, config->step_pin, 0, 0},
                                        {id, JW_RESOURCE_PIN, config->dir_pin, 0, 0},
                                        {id, JW_RESOURCE_PIN, config->enable_pin, 0, 0}};
  jw_resource_conflict_t conflict{};
  const auto status = jw_resource_replace(resources, resource_capacity, id, claims, 3, &conflict);
  auto result = jw_actuator_sync_result(status);
  if (status != JW_STATUS_OK) {
    result.conflict = conflict;
    return result;
  }
  // Validate all resources before changing output latches or configuration.
  StepDirDisable(channel);
  StepDirConfigure(channel, config);
  return result;
}
jw_actuator_result_t SetTarget(void* context, const jw_set_target_t* target) {
  if (target->mode == JW_MODE_POSITION &&
      (double(target->value) < INT32_MIN || double(target->value) > INT32_MAX))
    return jw_actuator_sync_result(JW_STATUS_INVALID_PAYLOAD);
  StepDirSetTarget(static_cast<ChannelState*>(context), target->mode, target->value);
  return jw_actuator_sync_result(JW_STATUS_OK);
}
jw_actuator_result_t Enable(void* context) {
  StepDirEnable(static_cast<ChannelState*>(context));
  return jw_actuator_sync_result(JW_STATUS_OK);
}
jw_actuator_result_t Disable(void* context) {
  StepDirDisable(static_cast<ChannelState*>(context));
  return jw_actuator_sync_result(JW_STATUS_OK);
}
jw_actuator_result_t Feedback(void* context, jw_feedback_t* value) {
  const auto* channel = static_cast<ChannelState*>(context);
  if (!channel->configured) return jw_actuator_sync_result(JW_STATUS_UNAVAILABLE);
  *value = channel->feedback;
  const uint64_t now = timestamp_us + uint32_t(micros() - previous_micros);
  if (now - value->metadata.timestamp_us > kMaxFeedbackAgeUs)
    value->metadata.validity = JW_SAMPLE_STALE;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
const jw_actuator_backend_ops_t kBackendOps = {
    State, Configure, SetTarget, Enable, Disable, Feedback};
}  // namespace

void StepDirDeviceInit(uint32_t boot_id, const char* name, bool (*valid_output_pin)(uint8_t)) {
  memset(&runtime, 0, sizeof(runtime));
  valid_pin = valid_output_pin;
  resources = g_stepdir_resources;
  resource_capacity = 3u * g_num_channels;
  extension = {};
  previous_micros = micros();
  timestamp_us = previous_micros;
  auto& identity = runtime.identity;
  identity.boot_id = boot_id ? boot_id : 1;
  identity.discovery_generation = 1;
  identity.function_count = g_num_channels;
  identity.max_frame_len = JW_MAX_FRAME_LEN;
  identity.max_inflight = 1;
  identity.max_page_entries = JW_DISCOVERY_PAGE_SIZE;
  identity.max_handler_us = 1000u * g_num_channels;
  identity.safety_flags = JW_SAFETY_BOARD_ESTOP;
  identity.protocol_min = identity.protocol_max = JW_PROTO_VERSION;
  identity.supported_planes = JW_PLANE_MESSAGE;
  strncpy(identity.fw_name, name, sizeof(identity.fw_name));
  const jw_actuator_schema_t description{JW_DRIVE_STEP_DIR,
                                         (1u << JW_MODE_POSITION) | (1u << JW_MODE_VELOCITY),
                                         JW_UNIT_NATIVE,
                                         JW_UNIT_NATIVE,
                                         JW_UNIT_NATIVE,
                                         JW_CLOCK_BOOT_MONOTONIC_US,
                                         kMaxFeedbackAgeUs};
  jw_encode_actuator_schema(schema, sizeof(schema), &description);
  runtime.functions = g_functions;
  runtime.safety = Safety;
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    memset(&g_channels[i], 0, sizeof(g_channels[i]));
    StepDirInit(&g_channels[i]);
    memset(&g_functions[i], 0, sizeof(g_functions[i]));
    auto& function = g_functions[i];
    function.descriptor = {i,
                           static_cast<uint16_t>(i + 1),
                           JW_PROFILE_ACTUATOR,
                           JW_PROFILE_VERSION_MAJOR,
                           JW_PROFILE_VERSION_MINOR,
                           31,
                           0};
    function.schema = schema;
    function.schema_len = sizeof(schema);
    g_channels[i].actuator = {&function, description, &kBackendOps, &g_channels[i]};
    function.handler = jw_actuator_handle;
    function.context = &g_channels[i].actuator;
    g_channels[i].feedback.metadata.boot_id = identity.boot_id;
    g_channels[i].feedback.metadata.clock = JW_CLOCK_BOOT_MONOTONIC_US;
  }
  jw_endpoint_init(&endpoint);
  jw_runtime_reset(&runtime);
}

void StepDirDeviceService() {
  const uint32_t now = micros();
  timestamp_us += uint32_t(now - previous_micros);
  previous_micros = now;
  if (extension.service) extension.service(extension.context, timestamp_us);
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    auto& channel = g_channels[i];
    StepDirService(&channel);
    channel.feedback.position = channel.step_dir.position_steps;
    // Open-loop estimate, not an encoder measurement.
    channel.feedback.velocity =
        channel.enabled && channel.target_mode == JW_MODE_VELOCITY ? channel.target_value : 0;
    auto& sample = channel.feedback.metadata;
    ++sample.sequence;
    sample.timestamp_us = timestamp_us;
    sample.validity = channel.configured ? JW_SAMPLE_VALID : JW_SAMPLE_INVALID;
  }
}

namespace {
int DeviceCommand(void*, const jw_command_t* command, uint8_t* response, size_t capacity) {
  int result =
      extension.command ? extension.command(extension.context, command, response, capacity) : -2;
  if (result == -2) result = jw_runtime_command(&runtime, command, response, capacity);
  if (result > 0 && extension.observed)
    extension.observed(extension.context, command, response, result);
  return result;
}
}  // namespace
int StepDirDeviceProcess(const uint8_t* request, size_t size, uint8_t* response, size_t capacity) {
  return jw_endpoint_process(
      &endpoint, request, size, response, capacity, DeviceCommand, jw_runtime_reset, &runtime);
}

void StepDirDeviceFault() {
  jw_runtime_fault(&runtime);
}

int StepDirDeviceExtend(jw_runtime_function_t* functions,
                        size_t capacity,
                        jw_resource_claim_t* table,
                        size_t slots,
                        StepDirDeviceExtension hooks) {
  if (!functions || !table || capacity <= g_num_channels || slots < 3u * g_num_channels + 2)
    return 0;
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    if (g_channels[i].configured || g_channels[i].enabled) return 0;
  }
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    functions[i] = runtime.functions[i];
    g_channels[i].actuator.function = &functions[i];
  }
  resources = table;
  resource_capacity = slots;
  jw_resource_table_init(resources, resource_capacity);
  runtime.functions = functions;
  extension = hooks;
  return 1;
}
jw_device_runtime_t* StepDirDeviceRuntime() {
  return &runtime;
}
uint64_t StepDirDeviceNowUs() {
  return timestamp_us + uint32_t(micros() - previous_micros);
}
