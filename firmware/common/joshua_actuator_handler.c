#include "joshua_actuator_handler.h"

jw_actuator_result_t jw_actuator_sync_result(jw_status_t status) {
  jw_actuator_result_t result = {0};
  result.status = status;
  result.execution = status == JW_STATUS_OUTCOME_UNKNOWN                   ? JW_EXECUTION_UNKNOWN
                     : status == JW_STATUS_OK || status == JW_STATUS_ERROR ? JW_EXECUTION_COMPLETE
                                                                           : JW_EXECUTION_REJECTED;
  return result;
}

static int reject(uint8_t* out, size_t cap, jw_status_t status) {
  return jw_encode_reply_payload(out, cap, status, JW_EXECUTION_REJECTED, NULL, 0);
}

int jw_actuator_handle(void* context,
                       const jw_command_t* command,
                       const jw_profile_request_t* request,
                       uint8_t* out,
                       size_t cap) {
  jw_actuator_handler_t* h = (jw_actuator_handler_t*)context;
  if (!h || !h->function || !h->ops || !h->ops->state || !command || !request || !out) return -1;
  if (command->channel != h->function->descriptor.function_id)
    return reject(out, cap, JW_STATUS_INVALID_CHANNEL);
  // Profile/version/operation admission is owned by jw_runtime_command.
  // Reserve the largest possible reply before any backend side effect.
  const bool feedback = request->operation == JW_ACTUATOR_GET_FEEDBACK;
  if (cap < JW_REPLY_HEADER_LEN + (feedback ? JW_FEEDBACK_RESPONSE_PAYLOAD_LEN : 4)) return -1;
  if (request->data_len && !request->data) return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);

  const jw_actuator_state_t before = h->ops->state(h->context);
  jw_actuator_result_t result;
  uint8_t data[JW_FEEDBACK_RESPONSE_PAYLOAD_LEN];
  size_t data_len = 0;
  switch (request->operation) {
    case JW_ACTUATOR_CONFIGURE: {
      if (h->schema.drive != JW_DRIVE_STEP_DIR || !h->ops->configure_step_dir)
        return reject(out, cap, JW_STATUS_UNSUPPORTED);
      jw_configure_step_dir_t config;
      if (jw_decode_configure_step_dir_payload(request->data, request->data_len, &config) !=
          JW_RESULT_OK)
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
      if (before.estopped) return reject(out, cap, JW_STATUS_UNAVAILABLE);
      if (before.enabled) return reject(out, cap, JW_STATUS_BUSY);
      result = h->ops->configure_step_dir(h->context, &config);
      break;
    }
    case JW_ACTUATOR_SET_TARGET: {
      jw_set_target_t target;
      if (jw_decode_set_target_payload(request->data, request->data_len, &target) != JW_RESULT_OK)
        return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
      if (!before.configured || before.estopped) return reject(out, cap, JW_STATUS_UNAVAILABLE);
      if (!(h->schema.target_modes & (1u << target.mode)) || !h->ops->set_target)
        return reject(out, cap, JW_STATUS_UNSUPPORTED);
      result = h->ops->set_target(h->context, &target);
      break;
    }
    case JW_ACTUATOR_GET_FEEDBACK: {
      if (request->data_len) return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
      if (!h->ops->feedback) return reject(out, cap, JW_STATUS_UNSUPPORTED);
      jw_feedback_t value;
      result = h->ops->feedback(h->context, &value);
      if (result.status == JW_STATUS_OK) {
        if (result.execution != JW_EXECUTION_COMPLETE) return -1;
        const int len = jw_encode_feedback_payload(data, sizeof(data), &value);
        if (len < 0) return -1;
        data_len = (size_t)len;
      }
      break;
    }
    case JW_ACTUATOR_ENABLE:
      if (request->data_len) return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
      if (!before.configured || before.estopped) return reject(out, cap, JW_STATUS_UNAVAILABLE);
      if (!h->ops->enable) return reject(out, cap, JW_STATUS_UNSUPPORTED);
      result = h->ops->enable(h->context);
      break;
    case JW_ACTUATOR_DISABLE:
      if (request->data_len) return reject(out, cap, JW_STATUS_INVALID_PAYLOAD);
      if (!h->ops->disable) return reject(out, cap, JW_STATUS_UNSUPPORTED);
      result = h->ops->disable(h->context);
      break;
    default:
      return reject(out, cap, JW_STATUS_UNSUPPORTED);
  }
  if (result.status == JW_STATUS_RESOURCE_CONFLICT) {
    data[0] = result.conflict.owner;
    data[1] = result.conflict.kind;
    data[2] = (uint8_t)result.conflict.id;
    data[3] = (uint8_t)(result.conflict.id >> 8);
    data_len = 4;
  }
  const int len =
      jw_encode_reply_payload(out, cap, result.status, result.execution, data, data_len);
  if (len < 0 || feedback || result.execution == JW_EXECUTION_REJECTED) return len;

  jw_device_status_t* state = &h->function->state;
  if (result.status == JW_STATUS_OK && result.execution == JW_EXECUTION_COMPLETE) {
    const jw_actuator_state_t after = h->ops->state(h->context);
    // A successful DISABLE must not hide a safety latch, dependency outage or
    // device fault. Recovery/authoritative backend updates clear those states.
    if (!after.estopped && state->state != JW_STATE_FAULT && state->state != JW_STATE_UNAVAILABLE) {
      state->state = after.enabled ? JW_STATE_ACTIVE : JW_STATE_DISABLED;
      state->recovery =
          after.configured ? 0 : JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE;
    }
  }
  ++state->epoch;
  state->action_session_id = command->session_id;
  state->action_message_id = command->message_id;
  state->action_status = result.status;
  state->action_execution = result.execution;
  return len;
}
