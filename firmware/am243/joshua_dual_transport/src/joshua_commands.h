// AM243's transport-neutral, software-only channel and command/reset callbacks.
// UART and EtherCAT artifacts reuse these semantics with separate lifecycle
// wrappers. No GPIO or TI SDK dependency.
//
// Ownership: every call and every read/write of a JoshuaChannel must use one
// externally serialized owner, including stop, reset, service and enabled reads.
// These functions have no internal locks and are non-reentrant. The UART task
// is its channel's sole owner; the TI EtherCAT adapter serializes its callbacks
// and higher-priority watchdog on one R5 core. Do not call from an independent
// ISR/FIQ/core. Simultaneous UART/EtherCAT ownership requires a shared arbiter;
// the JW EtherCAT artifact does not start UART.
//
// Callback context is a borrowed, non-null JoshuaChannel*. The generic C
// endpoint/profile stores it as void* to avoid depending on this board's type;
// the adapter casts it back without allocating or transferring ownership.
// The initialized channel must outlive the endpoint/profile and all callbacks.
#pragma once

#include <stdbool.h>

#include "joshua_device_runtime.h"

typedef struct {
  jw_device_runtime_t runtime;
  jw_runtime_function_t function;
  uint8_t schema[JW_ACTUATOR_SCHEMA_LEN];
  jw_feedback_t feedback;
  bool configured;
  bool enabled;
  bool estopped;
  uint16_t fault_flags;
  jw_configure_step_dir_t config;
  jw_mode_t target_mode;
  float target_value;
} JoshuaChannel;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a disabled software actuator at function ID 0.
 *
 * Clears all previous channel state, including configuration, feedback and fault
 * and emergency-stop latches, and installs the runtime callbacks and schema.
 * Initialize fresh or stopped storage only; do not reinitialize a live safety latch.
 *
 * @param[out] channel Non-null channel storage. Keep it at the same address after
 * initialization because the runtime retains pointers into this storage.
 * @param[in] boot_id Boot identity; zero is replaced with 1.
 * @param[in] planes Supported-plane bitmask using JW_PLANE_MESSAGE and/or JW_PLANE_CYCLIC.
 * @return None.
 */
void JoshuaInit(JoshuaChannel* channel, uint32_t boot_id, uint32_t planes);

/**
 * @brief Dispatch a decoded command through the channel's shared runtime.
 *
 * Handles discovery, status, safety and actuator-profile commands. The reply
 * contains the common status/execution header and any command-specific data;
 * the transport endpoint supplies framing and session handling.
 *
 * @param[in,out] context Non-null JoshuaChannel pointer initialized by JoshuaInit or JoshuaReset.
 * @param[in] frame Non-null decoded command; its payload is borrowed for this call only.
 * @param[out] response Non-null buffer receiving the encoded reply payload.
 * @param[in] capacity Size of response in bytes.
 * @return Encoded reply length in bytes, or -1 if encoding fails or capacity is
 * insufficient. Protocol rejections are reported in the encoded reply status.
 */
int JoshuaCommand(void* context, const jw_command_t* frame, uint8_t* response, size_t capacity);

/**
 * @brief Reset runtime state, disabling the actuator and discarding its configuration and target.
 *
 * An initialized channel retains its identity, feedback, fault flags and
 * emergency-stop latch. A zero-initialized channel is first initialized with
 * boot ID 1 and both message and cyclic planes.
 *
 * @param[in,out] context Non-null pointer to a zero-initialized or initialized JoshuaChannel.
 * @return None.
 */
void JoshuaReset(void* context);

/**
 * @brief Refresh cached software feedback outside command dispatch.
 *
 * Samples position or velocity from the commanded target, updates the timestamp
 * and increments the sample sequence. No physical motion is measured.
 *
 * @param[in,out] channel Non-null pointer to an initialized JoshuaChannel.
 * @param[in] now_us Current monotonic time in microseconds for the feedback sample.
 * @return None.
 */
void JoshuaService(JoshuaChannel* channel, uint64_t now_us);

/**
 * @brief Latch a software emergency stop and accumulate fault flags for EtherCAT safety handling.
 *
 * Disables the actuator, discards its configuration, clears its target and
 * copies the accumulated fault flags into feedback. Repeated calls are safe
 * under the serialized owner and never recover implicitly.
 *
 * @param[in,out] context Non-null pointer to an initialized JoshuaChannel.
 * @param[in] faults Fault bitmask to OR into the existing flags; zero still triggers a stop.
 * @return None.
 * @note A physical backend must disable hardware synchronously, not merely
 * change these software state fields. Stop scheduling and priority belong to the owner.
 */
void JoshuaStop(void* context, uint16_t faults);

/**
 * @brief Query whether the software actuator is enabled.
 *
 * @param[in] context Non-null pointer to an initialized JoshuaChannel; it is not modified.
 * @param[in] channel Function ID to query; only ID 0 is supported.
 * @return true if channel is 0 and the actuator is enabled; false otherwise.
 */
bool JoshuaEnabled(void* context, uint8_t channel);
#ifdef __cplusplus
}
#endif
