// Actuator-specific JoshuaWire interfaces, target/feedback values and configuration.
// Semantic structs only; payload codecs serialize fields explicitly.
#pragma once

#include <stdint.h>

#include "joshua_wire_sensor.h"

#define JW_FEEDBACK_RESPONSE_PAYLOAD_LEN (10 + JW_MEASUREMENT_METADATA_LEN)
#define JW_TARGET_FLOAT_WIRE_LEN 4  // Target values are serialized as f32 little-endian.
#define JW_SET_TARGET_PAYLOAD_LEN (1 + JW_TARGET_FLOAT_WIRE_LEN)
#define JW_SET_TARGET_COMPLEX_PAYLOAD_LEN (1 + 3 * JW_TARGET_FLOAT_WIRE_LEN)
#define JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN 11

// Target modes reserve zero as invalid. Map host TargetMode
// values explicitly; these IDs differ from the legacy JW1 mode values.
typedef enum {
  JW_MODE_INVALID = 0,
  JW_MODE_POSITION = 1,
  JW_MODE_VELOCITY = 2,
  JW_MODE_TORQUE = 3,
  JW_MODE_COMPLEX = 4,
} jw_mode_t;

// Mirrors robot.board.DriveInterface (robot/board/proto/board.proto)
// value-for-value. Only STEP_DIR is produced by firmware today; the rest
// are reserved so a future backend can report itself without a new wire
// version.
typedef enum {
  JW_DRIVE_INVALID = 0,
  JW_DRIVE_STEP_DIR = 1,
  JW_DRIVE_PWM_DC = 2,
  JW_DRIVE_SERVO_BUS_UART = 3,
  JW_DRIVE_CAN = 4,
  JW_DRIVE_PDO_JOINT = 5,
} jw_drive_t;

typedef struct {
  float position;
  float velocity;
  uint16_t fault_flags;
  jw_measurement_metadata_t metadata;
} jw_feedback_t;

// One channel's combined target, corresponding to one index in the ROS 2
// JointState position/velocity/effort arrays (effort is torque for revolute joints).
// All three fields are present; zero is a value, never an omitted field.
// Values use the channel's native units; ROS adapters must convert explicitly.
typedef struct {
  float position;
  float velocity;
  float torque;
} jw_complex_target_t;

// Mode bits are advertised in jw_actuator_schema_t; validate all fields before
// applying a target. Native units must be defined by that profile's metadata.
typedef struct {
  jw_mode_t mode;
  // Used by scalar modes; zero when decoding JW_MODE_COMPLEX.
  float value;
  // Used by JW_MODE_COMPLEX; zeroed when decoding scalar modes.
  jw_complex_target_t combined;
} jw_set_target_t;

typedef struct {
  uint32_t max_pulse_rate_hz;
  uint8_t invert_dir;
  uint8_t enable_active_low;
  // GPIO pin mapping, host-configured (docs/BOARD_LAYER_RFC.md §7.5,
  // revised — see robot/board/proto/board.proto's StepDirConfig comment
  // for why this moved out of the firmware image). MCU pin numbers, e.g.
  // Teensy 4.1 digital pin numbers.
  uint8_t step_pin;
  uint8_t dir_pin;
  uint8_t enable_pin;
  // STEP pulse HIGH width, microseconds. Host-configured, same reasoning
  // as the pins above. 0 means "use firmware's own default" (see
  // backend_stepdir.cpp) — not "zero-width pulse".
  uint16_t step_pulse_width_us;
} jw_configure_step_dir_t;

#define JW_ACTUATOR_SCHEMA_LEN 16
typedef struct {
  jw_drive_t drive;       // Actuator-only metadata, not a core compatibility gate.
  uint32_t target_modes;  // Bit N advertises JW_MODE_N.
  uint16_t position_unit;
  uint16_t velocity_unit;
  uint16_t effort_unit;
  jw_clock_t feedback_clock;
  uint32_t max_feedback_age_us;
} jw_actuator_schema_t;
