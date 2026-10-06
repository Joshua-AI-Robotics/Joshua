#include "joshua_serial_task.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>
#include <stddef.h>
#include <string.h>

#include "joshua_commands.h"
#include "joshua_wire_endpoint.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "transport_uart.h"

#define JOSHUA_SERIAL_TASK_STACK_SIZE (4096U)
#define JOSHUA_SERIAL_TASK_PRIORITY (TaskP_PRIORITY_HIGHEST - 4U)

static uint8_t gJoshuaSerialTaskStack[JOSHUA_SERIAL_TASK_STACK_SIZE] __attribute__((aligned(32)));
static TaskP_Object gJoshuaSerialTaskObject;
static JoshuaChannel gJoshuaChannel;

static void JoshuaSerialTask(void* args) {
  uint8_t request[JW_MAX_FRAME_LEN];
  uint8_t response[JW_MAX_FRAME_LEN];
  size_t pending_response_length = 0;
  jw_endpoint_t endpoint;
  JoshuaUartTransport uart;
  (void)args;
  const frame_transport_t transport =
      JoshuaUartTransportInit(&uart, UART_getBaseAddr(gUartHandle[CONFIG_UART_CONSOLE]), 20U);
  memset(&gJoshuaChannel, 0, sizeof(gJoshuaChannel));
  jw_endpoint_init(&endpoint);

  for (;;) {
    if (pending_response_length == 0) {
      size_t request_length = 0;
      if (transport.poll_receive(transport.context, request, sizeof(request), &request_length) ==
          FRAME_OK) {
        const int length = jw_endpoint_process(&endpoint,
                                               request,
                                               request_length,
                                               response,
                                               sizeof(response),
                                               JoshuaCommand,
                                               JoshuaReset,
                                               &gJoshuaChannel);
        if (length > 0) pending_response_length = (size_t)length;
      }
    }
    if (pending_response_length != 0 &&
        transport.try_send(transport.context, response, pending_response_length) == FRAME_OK) {
      pending_response_length = 0;
    }
    // This profile has a software-only channel; yield to other RTOS services.
    // All transport calls above return without waiting for bytes or FIFO space.
    ClockP_usleep(1000U);
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
