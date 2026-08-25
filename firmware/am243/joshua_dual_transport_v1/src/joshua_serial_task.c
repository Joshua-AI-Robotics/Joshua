#include "joshua_serial_task.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <drivers/uart.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>

#include "joshua_wire_v1.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"

#define JOSHUA_SERIAL_TASK_STACK_SIZE (4096U)
#define JOSHUA_SERIAL_TASK_PRIORITY (TaskP_PRIORITY_HIGHEST - 4U)
#define JOSHUA_SERIAL_CHANNEL_COUNT (1U)

typedef struct {
  bool configured;
  bool enabled;
  jw1_configure_step_dir_t config;
  jw1_mode_t target_mode;
  float target_value;
} JoshuaSerialChannel;

static uint8_t gJoshuaSerialTaskStack[JOSHUA_SERIAL_TASK_STACK_SIZE]
    __attribute__((aligned(32)));
static TaskP_Object gJoshuaSerialTaskObject;
static JoshuaSerialChannel gJoshuaSerialChannel;

static bool JoshuaUartReadExact(uint8_t* data, size_t size) {
  UART_Transaction transaction;
  UART_Transaction_init(&transaction);
  transaction.buf = data;
  transaction.count = size;
  return UART_read(gUartHandle[CONFIG_UART_CONSOLE], &transaction) == SystemP_SUCCESS;
}

static bool JoshuaUartWrite(const uint8_t* data, size_t size) {
  UART_Transaction transaction;
  UART_Transaction_init(&transaction);
  transaction.buf = (void*)data;
  transaction.count = size;
  return UART_write(gUartHandle[CONFIG_UART_CONSOLE], &transaction) == SystemP_SUCCESS;
}

static bool JoshuaReadFrame(uint8_t* buffer, size_t capacity, jw1_frame_t* frame) {
  uint8_t byte = 0U;
  for (;;) {
    if (!JoshuaUartReadExact(&byte, 1U)) {
      return false;
    }
    if (byte == JW1_SYNC_BYTE) {
      break;
    }
  }

  buffer[0] = byte;
  if (!JoshuaUartReadExact(&buffer[1], 1U)) {
    return false;
  }
  const size_t remaining = (size_t)buffer[1] + 2U;
  if (buffer[1] < 3U || remaining + 2U > capacity) {
    return false;
  }
  if (!JoshuaUartReadExact(&buffer[2], remaining)) {
    return false;
  }
  return jw1_decode_frame(buffer, remaining + 2U, frame) == 0;
}

static void JoshuaRespondStatus(uint8_t command, uint8_t channel, jw1_status_t status) {
  uint8_t response[JW1_MAX_FRAME_LEN];
  const int length =
      jw1_encode_status_response(response, sizeof(response), command, channel, status);
  if (length > 0) {
    (void)JoshuaUartWrite(response, (size_t)length);
  }
}

static void JoshuaHandleIdentify(void) {
  jw1_identify_response_t identify;
  uint8_t response[JW1_MAX_FRAME_LEN];
  memset(&identify, 0, sizeof(identify));
  identify.board_id = JW1_BOARD_AM243;
  memcpy(identify.fw_name, "am243-dual-v1", 13U);
  identify.n_channels = JOSHUA_SERIAL_CHANNEL_COUNT;
  identify.channel_drives[0] = JW1_DRIVE_STEP_DIR;

  const int length = jw1_encode_identify_response(response, sizeof(response), &identify);
  if (length > 0) {
    (void)JoshuaUartWrite(response, (size_t)length);
  }
}

static void JoshuaHandleConfigure(const jw1_frame_t* frame) {
  if (frame->channel != 0U ||
      jw1_decode_configure_channel_step_dir(frame, &gJoshuaSerialChannel.config) != 0) {
    JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_ERROR);
    return;
  }
  gJoshuaSerialChannel.configured = true;
  JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_OK);
}

static void JoshuaHandleSetTarget(const jw1_frame_t* frame) {
  jw1_set_target_t target;
  if (frame->channel != 0U || jw1_decode_set_target(frame, &target) != 0) {
    JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_ERROR);
    return;
  }
  gJoshuaSerialChannel.target_mode = target.mode;
  gJoshuaSerialChannel.target_value = target.value;
  JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_OK);
}

static void JoshuaHandleFeedback(const jw1_frame_t* frame) {
  if (frame->channel != 0U) {
    JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_ERROR);
    return;
  }

  jw1_feedback_t feedback;
  uint8_t response[JW1_MAX_FRAME_LEN];
  memset(&feedback, 0, sizeof(feedback));
  if (gJoshuaSerialChannel.target_mode == JW1_MODE_POSITION) {
    feedback.position = gJoshuaSerialChannel.target_value;
  } else if (gJoshuaSerialChannel.target_mode == JW1_MODE_VELOCITY) {
    feedback.velocity = gJoshuaSerialChannel.target_value;
  }
  const int length =
      jw1_encode_feedback_response(response, sizeof(response), frame->channel, &feedback);
  if (length > 0) {
    (void)JoshuaUartWrite(response, (size_t)length);
  }
}

static void JoshuaDispatch(const jw1_frame_t* frame) {
  switch (frame->cmd) {
    case JW1_CMD_IDENTIFY:
      JoshuaHandleIdentify();
      break;
    case JW1_CMD_CONFIGURE_CHANNEL:
      JoshuaHandleConfigure(frame);
      break;
    case JW1_CMD_SET_TARGET:
      JoshuaHandleSetTarget(frame);
      break;
    case JW1_CMD_GET_FEEDBACK:
      JoshuaHandleFeedback(frame);
      break;
    case JW1_CMD_ENABLE:
      if (frame->channel == 0U && gJoshuaSerialChannel.configured) {
        gJoshuaSerialChannel.enabled = true;
        JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_OK);
      } else {
        JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_ERROR);
      }
      break;
    case JW1_CMD_DISABLE:
      if (frame->channel == 0U) {
        gJoshuaSerialChannel.enabled = false;
        JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_OK);
      } else {
        JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_ERROR);
      }
      break;
    case JW1_CMD_ESTOP:
      gJoshuaSerialChannel.enabled = false;
      JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_OK);
      break;
    default:
      JoshuaRespondStatus(frame->cmd, frame->channel, JW1_STATUS_UNSUPPORTED);
      break;
  }
}

static void JoshuaSerialTask(void* args) {
  uint8_t buffer[JW1_MAX_FRAME_LEN];
  jw1_frame_t frame;
  (void)args;
  memset(&gJoshuaSerialChannel, 0, sizeof(gJoshuaSerialChannel));

  for (;;) {
    if (JoshuaReadFrame(buffer, sizeof(buffer), &frame)) {
      JoshuaDispatch(&frame);
    }
  }
}

int32_t JoshuaSerialStart(void) {
  TaskP_Params parameters;
  TaskP_Params_init(&parameters);
  parameters.name = "joshua_serial";
  parameters.stackSize = JOSHUA_SERIAL_TASK_STACK_SIZE;
  parameters.stack = gJoshuaSerialTaskStack;
  parameters.priority = JOSHUA_SERIAL_TASK_PRIORITY;
  parameters.taskMain = JoshuaSerialTask;
  parameters.args = NULL;
  return TaskP_construct(&gJoshuaSerialTaskObject, &parameters);
}

void JoshuaSerialDisableLogs(void) {
  (void)DebugP_logZoneDisable(0xFFFFFFFFU);
}

void JoshuaSerialDiscardLog(void* context, const char* format, va_list args) {
  (void)context;
  (void)format;
  (void)args;
}
