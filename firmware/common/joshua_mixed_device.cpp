#include "joshua_mixed_device.h"

#include <string.h>

namespace {
void Safety(void* context, int reason) {
  auto* s = static_cast<JoshuaMixedDevice*>(context);
  jw_scalar_sensor_safety(&s->sensor, reason);
  jw_watchdogs_reset(&s->watchdogs);
}
void Service(void* context, uint64_t now) {
  auto* s = static_cast<JoshuaMixedDevice*>(context);
  uint8_t enabled = 0, moving = 0;
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    const auto& channel = g_channels[i];
    if (!channel.enabled) continue;
    enabled |= 1u << i;
    if ((channel.target_mode == JW_MODE_POSITION &&
         channel.step_dir.position_steps != channel.target_value) ||
        (channel.target_mode == JW_MODE_VELOCITY && channel.target_value != 0))
      moving |= 1u << i;
  }
  if (jw_watchdogs_expired(
          &s->watchdogs, now, enabled, moving, s->sensor.subscription.state.active)) {
    StepDirDeviceFault();
    return;
  }
  jw_scalar_sensor_service(&s->sensor, now);
}
int Command(void* context, const jw_command_t* command, uint8_t* out, size_t capacity) {
  auto* s = static_cast<JoshuaMixedDevice*>(context);
  if (command->cmd == JW_CMD_CONFIGURE_WATCHDOGS) {
    jw_watchdog_policy_t policy{};
    if (jw_decode_watchdog_policy(command->payload, command->payload_len, &policy) != JW_RESULT_OK)
      return jw_encode_reply_payload(
          out, capacity, JW_STATUS_INVALID_PAYLOAD, JW_EXECUTION_REJECTED, nullptr, 0);
    if (capacity < JW_REPLY_HEADER_LEN + JW_WATCHDOG_POLICY_LEN) return -1;
    if (StepDirDeviceRuntime()->estopped)
      return jw_encode_reply_payload(
          out, capacity, JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED, nullptr, 0);
    for (uint8_t i = 0; i < g_num_channels; ++i)
      if (g_channels[i].enabled)
        return jw_encode_reply_payload(
            out, capacity, JW_STATUS_BUSY, JW_EXECUTION_REJECTED, nullptr, 0);
    jw_watchdogs_configure(&s->watchdogs, policy, StepDirDeviceNowUs());
    return jw_encode_reply_payload(
        out, capacity, JW_STATUS_OK, JW_EXECUTION_COMPLETE, command->payload, command->payload_len);
  }
  if (!s->watchdogs.configured && command->cmd == JW_CMD_PROFILE) {
    jw_profile_request_t p{};
    if (jw_decode_profile_request(command->payload, command->payload_len, &p) == JW_RESULT_OK &&
        ((p.profile_id == JW_PROFILE_ACTUATOR && p.operation == JW_ACTUATOR_ENABLE) ||
         (p.profile_id == JW_PROFILE_SENSOR && p.operation == JW_SENSOR_SUBSCRIBE)))
      return jw_encode_reply_payload(
          out, capacity, JW_STATUS_NOT_READY, JW_EXECUTION_REJECTED, nullptr, 0);
  }
  return -2;
}
void Observed(void* context, const jw_command_t* command, const uint8_t* out, size_t length) {
  auto* s = static_cast<JoshuaMixedDevice*>(context);
  jw_reply_t reply{};
  if (jw_decode_reply_payload(out, length, &reply) != JW_RESULT_OK || reply.status != JW_STATUS_OK)
    return;
  const auto now = StepDirDeviceNowUs();
  jw_watchdogs_request(&s->watchdogs, now);
  jw_profile_request_t p{};
  if (command->cmd == JW_CMD_PROFILE && command->channel < g_num_channels &&
      jw_decode_profile_request(command->payload, command->payload_len, &p) == JW_RESULT_OK &&
      p.profile_id == JW_PROFILE_ACTUATOR &&
      (p.operation == JW_ACTUATOR_SET_TARGET || p.operation == JW_ACTUATOR_ENABLE))
    jw_watchdogs_target(&s->watchdogs, command->channel, now);
}
}  // namespace
int JoshuaMixedDeviceInit(JoshuaMixedDevice* s, uint16_t pin, jw_scalar_acquisition_t acquisition) {
  if (!s || g_num_channels > JW_WATCHDOG_MAX_ACTUATORS) return 0;
  memset(s, 0, sizeof(*s));
  if (!StepDirDeviceExtend(s->functions,
                           JW_WATCHDOG_MAX_ACTUATORS + 1,
                           s->resources,
                           3 * JW_WATCHDOG_MAX_ACTUATORS + 2,
                           {s, Safety, Service, Command, Observed}))
    return 0;
  auto* runtime = StepDirDeviceRuntime();
  if (!jw_scalar_sensor_init(&s->sensor,
                             &s->functions[g_num_channels],
                             JW_REFERENCE_ADC_FUNCTION,
                             JW_REFERENCE_ADC_FUNCTION,
                             pin,
                             runtime->identity.boot_id,
                             JW_REFERENCE_ADC_MIN_PERIOD_US,
                             JW_REFERENCE_ADC_MAX_PERIOD_US,
                             JW_REFERENCE_ADC_TIMEOUT_US,
                             JW_REFERENCE_ADC_MAX_AGE_US,
                             JW_UNIT_NATIVE,
                             acquisition,
                             s->resources,
                             3 * JW_WATCHDOG_MAX_ACTUATORS + 2))
    return 0;
  runtime->identity.function_count = g_num_channels + 1;
  runtime->identity.safety_flags |= JW_SAFETY_CONFIGURABLE_WATCHDOGS | JW_SAFETY_COMM_INTERRUPTED;
  jw_runtime_reset(runtime);
  return 1;
}
int JoshuaMixedDeviceSample(void* context, uint8_t* out, size_t cap, uint32_t* seq) {
  return jw_scalar_sensor_prepare(&static_cast<JoshuaMixedDevice*>(context)->sensor, out, cap, seq);
}
void JoshuaMixedDeviceSubmitted(void* context, uint32_t seq) {
  jw_scalar_sensor_submitted(&static_cast<JoshuaMixedDevice*>(context)->sensor, seq);
}
