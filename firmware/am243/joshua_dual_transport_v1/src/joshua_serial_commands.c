// Implements AM243's software-only serial channel commands and session reset.
// Extracted from the UART task for reuse by v1/v2 artifacts and native tests;
// this handler neither drives motor GPIO nor changes the TI EtherCAT demo state.
// TODO(JoshuaWire v2 EtherCAT migration): Replace this implementation with the
// shared firmware dispatcher described in joshua_serial_commands.h, rather than
// duplicating command logic/state for EtherCAT. Physical motion also requires a
// real motor backend; the software-only feedback here is not measured motion.
#include "joshua_serial_commands.h"

#include <math.h>
#include <string.h>

void JoshuaSerialReset(void* context) {
  JoshuaSerialChannel* channel = (JoshuaSerialChannel*)context;
  const bool latch_estop = channel->latch_estop;
  memset(channel, 0, sizeof(*channel));
  channel->latch_estop = latch_estop;
}

// Software-only channel for the existing demo overlay; no GPIO output.
int JoshuaSerialCommand(void* context,
                        const jw1_frame_t* frame,
                        uint8_t* response,
                        size_t capacity) {
  JoshuaSerialChannel* channel = (JoshuaSerialChannel*)context;
  jw_status_t status = JW_STATUS_ERROR;
  switch (frame->cmd) {
    case JW_CMD_IDENTIFY: {
      if (frame->channel != JW_CHANNEL_NONE || frame->payload_len != 0) break;
      jw_identify_response_t identity;
      memset(&identity, 0, sizeof(identity));
      identity.board_id = JW_BOARD_AM243;
      memcpy(identity.fw_name, channel->latch_estop ? "am243-dual-v2" : "am243-dual-v1", 13);
      identity.n_channels = 1;
      identity.channel_drives[0] = JW_DRIVE_STEP_DIR;
      return jw1_encode_identify_response(response, capacity, &identity);
    }
    case JW_CMD_CONFIGURE_CHANNEL:
      if (frame->channel == 0 && !channel->estopped &&
          jw1_decode_configure_channel_step_dir(frame, &channel->config) == 0) {
        channel->configured = true;
        channel->enabled = false;
        status = JW_STATUS_OK;
      }
      break;
    case JW_CMD_SET_TARGET: {
      jw_set_target_t target;
      if (frame->channel != 0 || !channel->configured || channel->estopped ||
          jw1_decode_set_target(frame, &target) != 0 || !isfinite(target.value))
        break;
      if (target.mode != JW_MODE_POSITION && target.mode != JW_MODE_VELOCITY) {
        status = JW_STATUS_UNSUPPORTED;
        break;
      }
      channel->target_mode = target.mode;
      channel->target_value = target.value;
      status = JW_STATUS_OK;
      break;
    }
    case JW_CMD_GET_FEEDBACK: {
      if (frame->channel != 0 || frame->payload_len != 0) break;
      jw_feedback_t feedback;
      memset(&feedback, 0, sizeof(feedback));
      if (channel->target_mode == JW_MODE_POSITION) feedback.position = channel->target_value;
      if (channel->target_mode == JW_MODE_VELOCITY) feedback.velocity = channel->target_value;
      return jw1_encode_feedback_response(response, capacity, frame->channel, &feedback);
    }
    case JW_CMD_ENABLE:
      if (frame->channel == 0 && frame->payload_len == 0 && channel->configured &&
          !channel->estopped) {
        channel->enabled = true;
        status = JW_STATUS_OK;
      }
      break;
    case JW_CMD_DISABLE:
      if (frame->channel == 0 && frame->payload_len == 0) {
        channel->enabled = false;
        status = JW_STATUS_OK;
      }
      break;
    case JW_CMD_ESTOP:
      if (frame->channel == JW_CHANNEL_NONE && frame->payload_len == 0) {
        channel->enabled = false;
        channel->estopped = channel->latch_estop;
        status = JW_STATUS_OK;
      }
      break;
    default:
      status = JW_STATUS_UNSUPPORTED;
      break;
  }
  return jw1_encode_status_response(response, capacity, frame->cmd, frame->channel, status);
}
