// Implements the shared Teensy/ESP32 command dispatcher using backend_stepdir.
// Handles channel configuration, targets, feedback and enable/ESTOP behavior;
// session reset disables old pins before invalidating their configuration.
// Wire decoding and duplicate-request handling belong to the shared JW endpoint.
#include "joshua_stepdir_commands.h"

#include <math.h>
#include <string.h>

#include "backend_stepdir.h"
#include "channel_table.h"

void JoshuaStepDirReset(void* context) {
  auto* protocol = static_cast<JoshuaStepDirProtocol*>(context);
  for (uint8_t i = 0; i < g_num_channels; ++i) {
    // Disable using the old pin configuration before forgetting it.
    StepDirDisable(&g_channels[i]);
    g_channels[i].configured = false;
    g_channels[i].target_mode = JW_MODE_POSITION;
    g_channels[i].target_value = static_cast<float>(g_channels[i].step_dir.position_steps);
  }
  protocol->estopped = false;
}

int JoshuaStepDirCommand(void* context,
                         const jw_command_t* frame,
                         uint8_t* response,
                         size_t capacity) {
  auto* protocol = static_cast<JoshuaStepDirProtocol*>(context);
  auto reply = [&](jw_status_t status) {
    return jw_encode_status_payload(status, response, capacity);
  };
  ChannelState* channel = frame->channel < g_num_channels ? &g_channels[frame->channel] : nullptr;
  switch (frame->cmd) {
    case JW_CMD_IDENTIFY: {
      if (frame->channel != JW_CHANNEL_NONE || frame->payload_len != 0) {
        return reply(JW_STATUS_ERROR);
      }
      jw_identify_response_t identity{};
      identity.board_id = protocol->board_id;
      identity.n_channels = g_num_channels;
      if (protocol->firmware_name != nullptr) {
        strncpy(identity.fw_name, protocol->firmware_name, sizeof(identity.fw_name));
      }
      for (uint8_t i = 0; i < g_num_channels; ++i) identity.channel_drives[i] = JW_DRIVE_STEP_DIR;
      return jw_encode_identify_payload(response, capacity, &identity);
    }
    case JW_CMD_CONFIGURE_CHANNEL: {
      jw_configure_step_dir_t config;
      if (channel == nullptr || protocol->estopped ||
          jw_decode_configure_step_dir_payload(frame->payload, frame->payload_len, &config) !=
              JW_RESULT_OK) {
        return reply(JW_STATUS_ERROR);
      }
      StepDirDisable(channel);
      StepDirConfigure(channel, &config);
      return reply(JW_STATUS_OK);
    }
    case JW_CMD_SET_TARGET: {
      jw_set_target_t target;
      if (channel == nullptr || !channel->configured || protocol->estopped ||
          jw_decode_set_target_payload(frame->payload, frame->payload_len, &target) !=
              JW_RESULT_OK ||
          !isfinite(target.value)) {
        return reply(JW_STATUS_ERROR);
      }
      if (target.mode != JW_MODE_POSITION && target.mode != JW_MODE_VELOCITY) {
        return reply(JW_STATUS_UNSUPPORTED);
      }
      StepDirSetTarget(channel, target.mode, target.value);
      return reply(JW_STATUS_OK);
    }
    case JW_CMD_GET_FEEDBACK: {
      if (channel == nullptr || frame->payload_len != 0) return reply(JW_STATUS_ERROR);
      jw_feedback_t feedback{};
      feedback.position = static_cast<float>(channel->step_dir.position_steps);
      feedback.velocity = channel->target_mode == JW_MODE_VELOCITY ? channel->target_value : 0.0f;
      return jw_encode_feedback_payload(response, capacity, &feedback);
    }
    case JW_CMD_ENABLE:
      if (channel == nullptr || !channel->configured || protocol->estopped ||
          frame->payload_len != 0) {
        return reply(JW_STATUS_ERROR);
      }
      StepDirEnable(channel);
      return reply(JW_STATUS_OK);
    case JW_CMD_DISABLE:
      if (channel == nullptr || frame->payload_len != 0) return reply(JW_STATUS_ERROR);
      StepDirDisable(channel);
      return reply(JW_STATUS_OK);
    case JW_CMD_ESTOP:
      if (frame->channel != JW_CHANNEL_NONE || frame->payload_len != 0) {
        return reply(JW_STATUS_ERROR);
      }
      for (uint8_t i = 0; i < g_num_channels; ++i) StepDirDisable(&g_channels[i]);
      protocol->estopped = true;
      return reply(JW_STATUS_OK);
    default:
      return reply(JW_STATUS_UNSUPPORTED);
  }
}
