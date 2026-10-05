// Transport-neutral AM243 command semantics, reused by UART and CoE/PDO.
// This remains a software channel: feedback is not measured motor motion.
#include "joshua_commands.h"

#include <math.h>
#include <string.h>

void JoshuaStop(void* context, uint16_t faults) {
  JoshuaChannel* channel = (JoshuaChannel*)context;
  channel->enabled = false;
  channel->estopped = true;
  channel->target_value = 0;
  channel->target_mode = JW_MODE_POSITION;
  channel->fault_flags |= faults;
}
bool JoshuaEnabled(void* context, uint8_t channel) {
  return channel == 0 && ((JoshuaChannel*)context)->enabled;
}

void JoshuaReset(void* context) {
  JoshuaChannel* channel = (JoshuaChannel*)context;
  memset(channel, 0, sizeof(*channel));
}

// Software-only channel for the existing demo overlay; no GPIO output.
int JoshuaCommand(void* context, const jw_command_t* frame, uint8_t* response, size_t capacity) {
  JoshuaChannel* channel = (JoshuaChannel*)context;
  jw_status_t status = JW_STATUS_ERROR;
  switch (frame->cmd) {
    case JW_CMD_IDENTIFY: {
      if (frame->channel != JW_CHANNEL_NONE || frame->payload_len != 0) break;
      jw_identify_response_t identity;
      memset(&identity, 0, sizeof(identity));
      identity.board_id = JW_BOARD_AM243;
      memcpy(identity.fw_name, "am243-dual", 11);
      identity.n_channels = 1;
      identity.channel_drives[0] = JW_DRIVE_STEP_DIR;
      return jw_encode_identify_payload(response, capacity, &identity);
    }
    case JW_CMD_CONFIGURE_CHANNEL:
      if (frame->channel == 0 && !channel->estopped &&
          jw_decode_configure_step_dir_payload(
              frame->payload, frame->payload_len, &channel->config) == 0) {
        channel->configured = true;
        channel->enabled = false;
        status = JW_STATUS_OK;
      }
      break;
    case JW_CMD_SET_TARGET: {
      jw_set_target_t target;
      if (frame->channel != 0 || !channel->configured || channel->estopped ||
          jw_decode_set_target_payload(frame->payload, frame->payload_len, &target) != 0 ||
          !isfinite(target.value))
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
      feedback.fault_flags = channel->fault_flags;
      if (channel->target_mode == JW_MODE_POSITION) feedback.position = channel->target_value;
      if (channel->target_mode == JW_MODE_VELOCITY) feedback.velocity = channel->target_value;
      return jw_encode_feedback_payload(response, capacity, &feedback);
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
        channel->estopped = true;
        status = JW_STATUS_OK;
      }
      break;
    default:
      status = JW_STATUS_UNSUPPORTED;
      break;
  }
  return jw_encode_status_payload(response, capacity, status);
}
