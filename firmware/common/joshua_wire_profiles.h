// Built-in profile registry and payload codecs; extensions need no core edits.
#pragma once
#include "joshua_wire_actuator.h"
#include "joshua_wire_commands.h"
#include "joshua_wire_io.h"
#include "joshua_wire_sensor.h"

#define JW_PROFILE_ACTUATOR 1
#define JW_PROFILE_SENSOR 2
#define JW_PROFILE_IO 3
#define JW_PROFILE_VERSION_MAJOR 1
#define JW_PROFILE_VERSION_MINOR 0
// Operations are scoped to a profile. Bit N in discovery advertises operation N.
#define JW_ACTUATOR_CONFIGURE 0
#define JW_ACTUATOR_SET_TARGET 1
#define JW_ACTUATOR_GET_FEEDBACK 2
#define JW_ACTUATOR_ENABLE 3
#define JW_ACTUATOR_DISABLE 4
#define JW_SENSOR_READ 0
#define JW_IO_READ 0
#define JW_IO_WRITE 1

#ifdef __cplusplus
extern "C" {
#endif
int jw_encode_actuator_schema(uint8_t* out, size_t cap, const jw_actuator_schema_t* value);
jw_result_t jw_decode_actuator_schema(const uint8_t* data, size_t len, jw_actuator_schema_t* out);
int jw_encode_sensor_schema(uint8_t* out, size_t cap, const jw_sensor_schema_t* value);
jw_result_t jw_decode_sensor_schema(const uint8_t* data, size_t len, jw_sensor_schema_t* out);
int jw_encode_measurement_metadata(uint8_t* out,
                                   size_t cap,
                                   const jw_measurement_metadata_t* value);
jw_result_t jw_decode_measurement_metadata(const uint8_t* data,
                                           size_t len,
                                           jw_measurement_metadata_t* out);
int jw_encode_io_schema(uint8_t* out, size_t cap, const jw_io_schema_t* value);
jw_result_t jw_decode_io_schema(const uint8_t* data, size_t len, jw_io_schema_t* out);
int jw_encode_io_sample(uint8_t* out, size_t cap, const jw_io_sample_t* value);
jw_result_t jw_decode_io_sample(const uint8_t* data,
                                size_t len,
                                jw_io_interface_t interface,
                                jw_io_sample_t* out);
int jw_encode_sensor_sample_payload(uint8_t* out,
                                    size_t cap,
                                    jw_sensor_interface_t interface,
                                    const jw_sensor_sample_t* value);
jw_result_t jw_decode_sensor_sample_payload(const uint8_t* data,
                                            size_t len,
                                            jw_sensor_interface_t interface,
                                            jw_sensor_sample_t* out);
int jw_encode_io_payload(uint8_t* out, size_t cap, const jw_io_value_t* value);
jw_result_t jw_decode_io_payload(const uint8_t* data,
                                 size_t len,
                                 jw_io_interface_t interface,
                                 jw_io_value_t* out);
// Actuator-only target/feedback operations; never reinterpret sensor/I/O data.
int jw_encode_feedback_payload(uint8_t* out, size_t cap, const jw_feedback_t* value);
jw_result_t jw_decode_feedback_payload(const uint8_t* data, size_t len, jw_feedback_t* out);
// Scalar layout: [mode:u8][value:f32le]. JW_MODE_COMPLEX requires the dedicated
// encoder below; passing it to the scalar encoder is an error.
int jw_encode_set_target_payload(uint8_t* out, size_t cap, jw_mode_t mode, float value);
// Complex layout: [JW_MODE_COMPLEX:u8][position:f32le][velocity:f32le][torque:f32le].
// A JointState array maps to separate channel frames, without batch atomicity.
int jw_encode_complex_target_payload(uint8_t* out, size_t cap, const jw_complex_target_t* value);
// Decodes either layout, requiring the exact length for the encoded mode.
jw_result_t jw_decode_set_target_payload(const uint8_t* data, size_t len, jw_set_target_t* out);
int jw_encode_configure_step_dir_payload(uint8_t* out,
                                         size_t cap,
                                         const jw_configure_step_dir_t* value);
jw_result_t jw_decode_configure_step_dir_payload(const uint8_t* data,
                                                 size_t len,
                                                 jw_configure_step_dir_t* out);

#ifdef __cplusplus
}
#endif
