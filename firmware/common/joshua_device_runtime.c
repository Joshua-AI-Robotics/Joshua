#include "joshua_device_runtime.h"

#include <string.h>

static int reply(uint8_t* out, size_t cap, jw_status_t status, const uint8_t* data, size_t len) {
  return jw_encode_reply_payload(out,
                                 cap,
                                 status,
                                 status == JW_STATUS_OK || status == JW_STATUS_ERROR
                                     ? JW_EXECUTION_COMPLETE
                                     : JW_EXECUTION_REJECTED,
                                 data,
                                 len);
}
static void transition(jw_device_status_t* state, int stopped, jw_state_reason_t reason) {
  ++state->epoch;
  state->state = stopped ? JW_STATE_UNAVAILABLE : JW_STATE_DISABLED;
  state->reason = reason;
  state->recovery = JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE |
                    (stopped ? JW_RECOVERY_CLEAR_ESTOP : 0);
  state->stop_phase = stopped ? JW_STOP_ACTION_COMPLETE : JW_STOP_NONE;
  state->verification = JW_SAFE_UNSUPPORTED;
  state->action_session_id = 0;
  state->action_message_id = 0;
  state->action_status = JW_STATUS_INVALID;
  state->action_execution = JW_EXECUTION_REJECTED;
}
void jw_runtime_reset(void* context) {
  jw_device_runtime_t* runtime = (jw_device_runtime_t*)context;
  if (runtime->safety) runtime->safety(runtime->context, 0);
  transition(&runtime->state, runtime->estopped, JW_REASON_SESSION_RESET);
  for (uint16_t i = 0; i < runtime->identity.function_count; ++i)
    transition(&runtime->functions[i].state, runtime->estopped, JW_REASON_SESSION_RESET);
}
void jw_runtime_fault(jw_device_runtime_t* runtime) {
  if (runtime->safety) runtime->safety(runtime->context, 1);
  runtime->estopped = 1;
  transition(&runtime->state, 1, JW_REASON_DEVICE_FAULT);
  for (uint16_t i = 0; i < runtime->identity.function_count; ++i)
    transition(&runtime->functions[i].state, 1, JW_REASON_DEVICE_FAULT);
}
int jw_runtime_command(void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
  jw_device_runtime_t* r = (jw_device_runtime_t*)context;
  uint8_t data[JW_MAX_PAYLOAD_LEN];
  int len = -1;
  jw_runtime_function_t* function = NULL;
  for (uint16_t i = 0; i < r->identity.function_count; ++i)
    if (r->functions[i].descriptor.function_id == command->channel) function = &r->functions[i];
  const int function_command =
      command->cmd == JW_CMD_PROFILE || command->cmd == JW_CMD_DESCRIBE_FUNCTION;
  if ((function_command && command->channel == JW_CHANNEL_NONE) ||
      (!function_command && command->cmd != JW_CMD_GET_STATUS &&
       command->channel != JW_CHANNEL_NONE))
    return reply(out, cap, JW_STATUS_INVALID_CHANNEL, NULL, 0);
  size_t expected = 0;
  switch (command->cmd) {
    case JW_CMD_DISCOVER:
    case JW_CMD_GET_DEPENDENCIES:
      expected = 6;
      break;
    case JW_CMD_ESTOP:
    case JW_CMD_RECOVER:
      expected = 2;
      break;
    case JW_CMD_PROFILE:
      expected = command->payload_len;
      break;
    default:
      break;
  }
  if (command->payload_len != expected || (expected && !command->payload))
    return reply(out, cap, JW_STATUS_INVALID_PAYLOAD, NULL, 0);
  switch (command->cmd) {
    case JW_CMD_IDENTIFY:
      len = jw_encode_identify_payload(data, sizeof(data), &r->identity);
      break;
    case JW_CMD_DISCOVER: {
      const uint8_t* p = command->payload;
      const uint32_t generation =
          (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      const uint16_t cursor = p[4] | ((uint16_t)p[5] << 8);
      if (generation != r->identity.discovery_generation && !(generation == 0 && cursor == 0))
        return reply(out, cap, JW_STATUS_STALE_DISCOVERY, NULL, 0);
      if (cursor > r->identity.function_count)
        return reply(out, cap, JW_STATUS_INVALID_PAYLOAD, NULL, 0);
      jw_discovery_page_t page = {0};
      page.generation = r->identity.discovery_generation;
      page.total = r->identity.function_count;
      while (cursor + page.count < page.total && page.count < r->identity.max_page_entries) {
        page.functions[page.count] = r->functions[cursor + page.count].descriptor;
        ++page.count;
      }
      page.next_cursor = cursor + page.count == page.total ? JW_CURSOR_END : cursor + page.count;
      len = jw_encode_discovery_page(data, sizeof(data), &page);
      break;
    }
    case JW_CMD_DESCRIBE_FUNCTION:
      if (!function) return reply(out, cap, JW_STATUS_INVALID_CHANNEL, NULL, 0);
      len = jw_encode_function_descriptor(data, sizeof(data), &function->descriptor);
      if (len < 0 || function->schema_len > sizeof(data) - (size_t)len - JW_REPLY_HEADER_LEN)
        return -1;
      memcpy(data + len, function->schema, function->schema_len);
      len += function->schema_len;
      break;
    case JW_CMD_GET_DEPENDENCIES: {
      const uint8_t* p = command->payload;
      const uint32_t generation =
          (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      const uint16_t cursor = p[4] | ((uint16_t)p[5] << 8);
      if (generation != r->identity.discovery_generation)
        return reply(out, cap, JW_STATUS_STALE_DISCOVERY, NULL, 0);
      if (cursor > r->identity.function_count)
        return reply(out, cap, JW_STATUS_INVALID_PAYLOAD, NULL, 0);
      jw_dependency_page_t page = {0};
      page.generation = generation;
      while (cursor + page.count < r->identity.function_count &&
             page.count < JW_DEPENDENCY_PAGE_SIZE) {
        page.dependencies[page.count].function_id =
            r->functions[cursor + page.count].descriptor.function_id;
        page.dependencies[page.count].effects = JW_SHUTDOWN_OUTPUT | JW_SHUTDOWN_EXECUTION;
        ++page.count;
      }
      page.next_cursor =
          cursor + page.count == r->identity.function_count ? JW_CURSOR_END : cursor + page.count;
      len = jw_encode_dependency_page(data, sizeof(data), &page);
      break;
    }
    case JW_CMD_GET_STATUS:
      if (command->channel != JW_CHANNEL_NONE && !function)
        return reply(out, cap, JW_STATUS_INVALID_CHANNEL, NULL, 0);
      len = jw_encode_device_status(data, sizeof(data), function ? &function->state : &r->state);
      break;
    case JW_CMD_ESTOP:
    case JW_CMD_RECOVER: {
      if (command->payload[0] || command->payload[1])
        return reply(out, cap, JW_STATUS_UNSUPPORTED, NULL, 0);
      const int stop = command->cmd == JW_CMD_ESTOP;
      if (r->safety) r->safety(r->context, stop ? 1 : 2);
      r->estopped = stop;
      transition(&r->state, stop, stop ? JW_REASON_ESTOP : JW_REASON_SESSION_RESET);
      r->state.action_session_id = command->session_id;
      r->state.action_message_id = command->message_id;
      r->state.action_execution = JW_EXECUTION_COMPLETE;
      r->state.action_status = JW_STATUS_OK;
      for (uint16_t i = 0; i < r->identity.function_count; ++i) {
        transition(&r->functions[i].state, stop, stop ? JW_REASON_ESTOP : JW_REASON_SESSION_RESET);
        r->functions[i].state.action_session_id = command->session_id;
        r->functions[i].state.action_message_id = command->message_id;
        r->functions[i].state.action_execution = JW_EXECUTION_COMPLETE;
        r->functions[i].state.action_status = JW_STATUS_OK;
      }
      len = 0;
      break;
    }
    case JW_CMD_PROFILE: {
      if (!function) return reply(out, cap, JW_STATUS_INVALID_CHANNEL, NULL, 0);
      jw_profile_request_t profile;
      if (jw_decode_profile_request(command->payload, command->payload_len, &profile) !=
          JW_RESULT_OK)
        return reply(out, cap, JW_STATUS_INVALID_PAYLOAD, NULL, 0);
      if (profile.profile_id != function->descriptor.profile_id ||
          profile.major != function->descriptor.profile_major ||
          profile.minor > function->descriptor.profile_minor)
        return reply(out, cap, JW_STATUS_PROFILE_MISMATCH, NULL, 0);
      if (!jw_function_supports_operation(&function->descriptor, &profile) || !function->handler)
        return reply(out, cap, JW_STATUS_UNSUPPORTED, NULL, 0);
      return function->handler(function->context, command, &profile, out, cap);
    }
    default:
      return reply(out, cap, JW_STATUS_UNSUPPORTED, NULL, 0);
  }
  return len < 0 ? -1 : reply(out, cap, JW_STATUS_OK, data, len);
}
