// Bounded software-only AM243 actuator backend; no GPIO output or measured motion.
#include "joshua_commands.h"

#include <string.h>

static int result(uint8_t* out, size_t cap, jw_status_t status, const uint8_t* data, size_t len) {
  return jw_encode_reply_payload(out,
                                 cap,
                                 status,
                                 status == JW_STATUS_OK || status == JW_STATUS_ERROR
                                     ? JW_EXECUTION_COMPLETE
                                     : JW_EXECUTION_REJECTED,
                                 data,
                                 len);
}
static int profile(void* context,
                   const jw_command_t* command,
                   const jw_profile_request_t* request,
                   uint8_t* out,
                   size_t cap) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  jw_status_t status = JW_STATUS_OK;
  uint8_t data[JW_FEEDBACK_RESPONSE_PAYLOAD_LEN];
  int data_len = 0;
  switch (request->operation) {
    case JW_ACTUATOR_CONFIGURE: {
      jw_configure_step_dir_t config;
      if (jw_decode_configure_step_dir_payload(request->data, request->data_len, &config) !=
          JW_RESULT_OK)
        status = JW_STATUS_INVALID_PAYLOAD;
      else if (c->estopped)
        status = JW_STATUS_UNAVAILABLE;
      else if (c->enabled)
        status = JW_STATUS_BUSY;
      else {
        c->config = config;
        c->configured = true;
        c->enabled = false;
      }
      break;
    }
    case JW_ACTUATOR_SET_TARGET: {
      jw_set_target_t target;
      if (jw_decode_set_target_payload(request->data, request->data_len, &target) != JW_RESULT_OK)
        status = JW_STATUS_INVALID_PAYLOAD;
      else if (!c->configured || c->estopped)
        status = JW_STATUS_UNAVAILABLE;
      else if (target.mode != JW_MODE_POSITION && target.mode != JW_MODE_VELOCITY)
        status = JW_STATUS_UNSUPPORTED;
      else {
        c->target_mode = target.mode;
        c->target_value = target.value;
        c->feedback.position = target.mode == JW_MODE_POSITION ? target.value : 0;
        c->feedback.velocity = target.mode == JW_MODE_VELOCITY ? target.value : 0;
        ++c->feedback.metadata.sequence;
        ++c->feedback.metadata.timestamp_us;
      }
      break;
    }
    case JW_ACTUATOR_GET_FEEDBACK: {
      if (request->data_len) {
        status = JW_STATUS_INVALID_PAYLOAD;
        break;
      }
      data_len = jw_encode_feedback_payload(data, sizeof(data), &c->feedback);
      break;
    }
    case JW_ACTUATOR_ENABLE:
      if (request->data_len)
        status = JW_STATUS_INVALID_PAYLOAD;
      else if (!c->configured || c->estopped)
        status = JW_STATUS_UNAVAILABLE;
      else
        c->enabled = true;
      break;
    case JW_ACTUATOR_DISABLE:
      if (request->data_len)
        status = JW_STATUS_INVALID_PAYLOAD;
      else
        c->enabled = false;
      break;
    default:
      status = JW_STATUS_UNSUPPORTED;
      break;
  }
  if (status == JW_STATUS_OK && request->operation != JW_ACTUATOR_GET_FEEDBACK) {
    jw_device_status_t* state = &c->function.state;
    ++state->epoch;
    state->state = c->enabled ? JW_STATE_ACTIVE : JW_STATE_DISABLED;
    state->recovery = c->configured ? 0 : JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE;
    state->action_session_id = command->session_id;
    state->action_message_id = command->message_id;
    state->action_status = status;
    state->action_execution = JW_EXECUTION_COMPLETE;
  }
  return data_len < 0 ? -1 : result(out, cap, status, data_len > 0 ? data : NULL, (size_t)data_len);
}
static void safety(void* context, int action) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  c->enabled = false;
  c->configured = false;
  c->target_value = 0;
  if (action == 1) c->estopped = true;
  if (action == 2) c->estopped = false;
}
void JoshuaInit(JoshuaChannel* c, uint32_t boot, uint32_t planes) {
  memset(c, 0, sizeof(*c));
  c->runtime.identity.boot_id = boot ? boot : 1;
  c->runtime.identity.discovery_generation = 1;
  c->runtime.identity.function_count = 1;
  c->runtime.identity.max_frame_len = JW_MAX_FRAME_LEN;
  c->runtime.identity.max_inflight = 1;
  c->runtime.identity.max_page_entries = JW_DISCOVERY_PAGE_SIZE;
  c->runtime.identity.max_handler_us = 100;
  c->runtime.identity.safety_flags = JW_SAFETY_BOARD_ESTOP;
  c->runtime.identity.protocol_min = c->runtime.identity.protocol_max = JW_PROTO_VERSION;
  c->runtime.identity.supported_planes = planes;
  memcpy(c->runtime.identity.fw_name, "am243-software", 14);
  c->runtime.functions = &c->function;
  c->runtime.safety = safety;
  c->runtime.context = c;
  c->function.descriptor.function_id = 0;
  c->function.descriptor.device_id = 1;
  c->function.descriptor.profile_id = JW_PROFILE_ACTUATOR;
  c->function.descriptor.profile_major = JW_PROFILE_VERSION_MAJOR;
  c->function.descriptor.operations = 31;
  const jw_actuator_schema_t schema = {JW_DRIVE_STEP_DIR,
                                       (1u << JW_MODE_POSITION) | (1u << JW_MODE_VELOCITY),
                                       JW_UNIT_NATIVE,
                                       JW_UNIT_NATIVE,
                                       JW_UNIT_NATIVE,
                                       JW_CLOCK_BOOT_MONOTONIC_US,
                                       100000};
  jw_encode_actuator_schema(c->schema, sizeof(c->schema), &schema);
  c->function.schema = c->schema;
  c->function.schema_len = sizeof(c->schema);
  c->function.handler = profile;
  c->function.context = c;
  c->feedback.metadata.validity = JW_SAMPLE_VALID;
  c->feedback.metadata.clock = JW_CLOCK_BOOT_MONOTONIC_US;
  c->feedback.metadata.boot_id = c->runtime.identity.boot_id;
  jw_runtime_reset(&c->runtime);
}
void JoshuaReset(void* context) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  if (!c->runtime.identity.boot_id)
    JoshuaInit(c, 1, JW_PLANE_MESSAGE | JW_PLANE_CYCLIC);
  else
    jw_runtime_reset(&c->runtime);
}
int JoshuaCommand(void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  return jw_runtime_command(&c->runtime, command, out, cap);
}
void JoshuaStop(void* context, uint16_t faults) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  safety(c, 1);
  c->runtime.estopped = 1;
  c->fault_flags |= faults;
  c->feedback.fault_flags = c->fault_flags;
}
bool JoshuaEnabled(void* context, uint8_t channel) {
  return channel == 0 && ((JoshuaChannel*)context)->enabled;
}
