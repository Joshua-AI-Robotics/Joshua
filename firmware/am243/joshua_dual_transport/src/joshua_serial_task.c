#include "joshua_serial_task.h"

#include <drivers/uart.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "joshua_commands.h"
#include "joshua_wire_serial_endpoint.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"

#define JOSHUA_SERIAL_TASK_STACK_SIZE (4096U)
#define JOSHUA_SERIAL_TASK_PRIORITY (TaskP_PRIORITY_HIGHEST - 4U)

static uint8_t gJoshuaSerialTaskStack[JOSHUA_SERIAL_TASK_STACK_SIZE] __attribute__((aligned(32)));
static TaskP_Object gJoshuaSerialTaskObject;
static JoshuaChannel gJoshuaChannel;

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

static size_t JoshuaReadFrame(uint8_t* buffer, size_t capacity) {
  uint8_t byte = 0U;
  for (;;) {
    if (!JoshuaUartReadExact(&byte, 1U)) {
      return false;
    }
    if (byte == JW_SYNC_BYTE) {
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
  return remaining + 2U;
}

static void JoshuaSerialTask(void* args) {
  uint8_t request[JW_MAX_FRAME_LEN];
  uint8_t response[JW_MAX_FRAME_LEN];
  jw_serial_endpoint_t endpoint;
  (void)args;
  memset(&gJoshuaChannel, 0, sizeof(gJoshuaChannel));
  jw_serial_endpoint_init(&endpoint);

  for (;;) {
    const size_t request_len = JoshuaReadFrame(request, sizeof(request));
    if (request_len != 0) {
      const int len = jw_serial_endpoint_process(&endpoint,
                                                 request,
                                                 request_len,
                                                 response,
                                                 sizeof(response),
                                                 JoshuaCommand,
                                                 JoshuaReset,
                                                 &gJoshuaChannel);
      if (len > 0) (void)JoshuaUartWrite(response, (size_t)len);
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
