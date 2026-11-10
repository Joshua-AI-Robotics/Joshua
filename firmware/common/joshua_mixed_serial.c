#include "joshua_mixed_serial.h"

#include <string.h>
void jw_mixed_serial_init(jw_mixed_serial_t* s,
                          frame_transport_t transport,
                          jw_mixed_serial_ops_t ops) {
  memset(s, 0, sizeof(*s));
  s->transport = transport;
  s->ops = ops;
}
void jw_mixed_serial_service(jw_mixed_serial_t* s) {
  s->ops.service(s->ops.context);
  uint8_t bytes[JW_MAX_FRAME_LEN];
  size_t count = 0;
  // Poll regardless of application reply occupancy: RX admission and the serial
  // adapter's independent TX-fault evaluation must keep making progress.
  const frame_status_t input =
      s->transport.poll_receive(s->transport.context, bytes, sizeof(bytes), &count);
  if (input == FRAME_OK) {
    jw_frame_t frame;
    if (jw_decode_frame(bytes, count, &frame) == JW_RESULT_OK &&
        frame.cmd != JW_CMD_STREAM_SAMPLE) {
      ++s->received;
      const int safety = frame.cmd == JW_CMD_ESTOP || frame.cmd == JW_CMD_RESET_SESSION;
      uint8_t* out = safety ? s->safety : s->ordinary;
      size_t* length = safety ? &s->safety_len : &s->ordinary_len;
      if (*length) {
        // No capacity means no dispatch/side effects. Preserve both retained
        // replies; fail outputs locally rather than lose reply ownership.
        ++s->admission_faults;
        s->ops.fault(s->ops.context);
      } else {
        const int result = s->ops.dispatch(s->ops.context, bytes, count, out, JW_MAX_FRAME_LEN);
        if (result > 0 && result <= JW_MAX_FRAME_LEN)
          *length = result;
        else if (result < 0 || result > JW_MAX_FRAME_LEN)
          s->ops.fault(s->ops.context);
      }
    }
  } else if (input == FRAME_ERROR)
    s->ops.fault(s->ops.context);
  uint8_t* reply = s->safety_len ? s->safety : s->ordinary;
  size_t* length = s->safety_len ? &s->safety_len : &s->ordinary_len;
  if (*length) {
    const frame_status_t status = s->transport.try_send(s->transport.context, reply, *length);
    if (status == FRAME_OK) {
      *length = 0;
      ++s->sent_replies;
    } else if (status == FRAME_ERROR)
      s->ops.fault(s->ops.context);
    return;
  }
  uint32_t sequence;
  const int sample = s->ops.sample(s->ops.context, bytes, sizeof(bytes), &sequence);
  if (sample < 0 || sample > JW_MAX_FRAME_LEN) {
    s->ops.fault(s->ops.context);
    return;
  }
  if (sample > 0) {
    const frame_status_t status = s->transport.try_send(s->transport.context, bytes, sample);
    if (status == FRAME_OK) {
      s->ops.submitted(s->ops.context, sequence);
      ++s->sent_samples;
    } else if (status == FRAME_ERROR)
      s->ops.fault(s->ops.context);
  }
}
