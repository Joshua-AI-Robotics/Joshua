// Bounded software-only AM243 actuator backend; no GPIO output or measured motion.
#include "am243_software_backend.h"

#include <string.h>

/**
 * @brief Snapshot the software actuator's admission state for the shared handler.
 * @param[in] context Non-null pointer to an initialized JoshuaChannel.
 * @return Current configured, enabled and emergency-stop flags; the channel is not modified.
 */
static jw_actuator_state_t state(void* context) {
  const JoshuaChannel* c = (const JoshuaChannel*)context;
  const jw_actuator_state_t value = {c->configured, c->enabled, c->estopped};
  return value;
}
/**
 * @brief Store a validated STEP/DIR configuration and leave the actuator disabled.
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @param[in] config Non-null configuration admitted by the shared actuator handler.
 * @return JW_STATUS_OK with JW_EXECUTION_COMPLETE; no GPIOs are configured.
 */
static jw_actuator_result_t configure(void* context, const jw_configure_step_dir_t* config) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  c->config = *config;
  c->configured = true;
  c->enabled = false;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
/**
 * @brief Store a validated target for later software feedback acquisition.
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @param[in] target Non-null target admitted by the shared actuator handler.
 * @return JW_STATUS_OK with JW_EXECUTION_COMPLETE; cached feedback is unchanged.
 */
static jw_actuator_result_t set_target(void* context, const jw_set_target_t* target) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  c->target_mode = target->mode;
  c->target_value = target->value;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
/**
 * @brief Copy cached software feedback without acquiring a new sample.
 * @param[in] context Non-null pointer to an initialized JoshuaChannel; it is not modified.
 * @param[out] value Non-null storage receiving the last sample produced by JoshuaService.
 * @return JW_STATUS_OK with JW_EXECUTION_COMPLETE.
 */
static jw_actuator_result_t feedback(void* context, jw_feedback_t* value) {
  *value = ((JoshuaChannel*)context)->feedback;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
/**
 * @brief Set the software enabled flag after admission by the shared handler.
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @return JW_STATUS_OK with JW_EXECUTION_COMPLETE; no physical output is driven.
 */
static jw_actuator_result_t enable(void* context) {
  ((JoshuaChannel*)context)->enabled = true;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
/**
 * @brief Clear the software enabled flag while retaining configuration and safety latches.
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @return JW_STATUS_OK with JW_EXECUTION_COMPLETE.
 */
static jw_actuator_result_t disable(void* context) {
  ((JoshuaChannel*)context)->enabled = false;
  return jw_actuator_sync_result(JW_STATUS_OK);
}
static const jw_actuator_backend_ops_t backend_ops = {
    state, configure, set_target, enable, disable, feedback};

/**
 * @brief Apply a runtime safety action, always disabling and unconfiguring the actuator.
 *
 * Clears the target value. Reset and stop retain cached feedback and fault
 * flags; recovery also clears the channel and feedback fault flags.
 *
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @param[in] action Runtime action: 0 resets while preserving the emergency-stop
 * latch, 1 sets the latch, and 2 clears it for recovery. No action enables the actuator.
 * @return None.
 */
static void safety(void* context, int action) {
  JoshuaChannel* c = (JoshuaChannel*)context;
  c->enabled = false;
  c->configured = false;
  c->target_value = 0;
  if (action == 1) c->estopped = true;
  if (action == 2) {
    c->estopped = false;
    c->fault_flags = 0;
    c->feedback.fault_flags = 0;
  }
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
  c->actuator.function = &c->function;
  c->actuator.schema = schema;
  c->actuator.ops = &backend_ops;
  c->actuator.context = c;
  c->function.handler = jw_actuator_handle;
  c->function.context = &c->actuator;
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
  c->function.state.state = JW_STATE_UNAVAILABLE;
  c->function.state.reason = faults ? JW_REASON_DEVICE_FAULT : JW_REASON_ESTOP;
  c->function.state.recovery =
      JW_RECOVERY_CLEAR_ESTOP | JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE;
  c->function.state.stop_phase = JW_STOP_ACTION_COMPLETE;
  c->function.state.verification = JW_SAFE_UNSUPPORTED;
  ++c->function.state.epoch;
  c->runtime.state = c->function.state;
}
bool JoshuaEnabled(void* context, uint8_t channel) {
  return channel == 0 && ((JoshuaChannel*)context)->enabled;
}

void JoshuaService(JoshuaChannel* c, uint64_t now_us) {
  c->feedback.position = c->target_mode == JW_MODE_POSITION ? c->target_value : 0;
  c->feedback.velocity = c->target_mode == JW_MODE_VELOCITY ? c->target_value : 0;
  c->feedback.metadata.timestamp_us = now_us;
  ++c->feedback.metadata.sequence;
}
