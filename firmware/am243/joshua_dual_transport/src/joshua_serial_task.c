// UART0 endpoint for the current correlated JoshuaWire protocol. The adjacent
// TI EtherCAT demo has independent, vendor-defined PDO state and no GPIO output.
#include "joshua_serial_task.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>
#include <stdbool.h>
#include <stddef.h>

#include "am243_software_backend.h"
#include "boot_identity.h"
#include "joshua_wire_endpoint.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "transport_uart.h"

#define JOSHUA_SERIAL_TASK_STACK_SIZE 4096U
#define JOSHUA_SERIAL_TASK_PRIORITY (TaskP_PRIORITY_HIGHEST - 4U)

static uint8_t task_stack[JOSHUA_SERIAL_TASK_STACK_SIZE] __attribute__((aligned(32)));
static TaskP_Object task_object;
static JoshuaChannel channel;
static jw_endpoint_t endpoint;
static uint32_t boot_id;

static void TransmitTimeout(void* context) {
  jw_runtime_fault(&((JoshuaChannel*)context)->runtime);
}

static void JoshuaSerialTask(void* args) {
  (void)args;
  JoshuaUartTransport uart;
  const frame_transport_t transport =
      JoshuaUartTransportInit(&uart, UART_getBaseAddr(gUartHandle[CONFIG_UART_CONSOLE]), 20u);
  JoshuaInit(&channel, boot_id, JW_PLANE_MESSAGE);
  serial_frame_transport_set_tx_timeout(
      &uart.serial, JW_SERIAL_TX_STOP_TIMEOUT_MS, TransmitTimeout, &channel);
  jw_endpoint_init(&endpoint);
  uint8_t response[JW_MAX_FRAME_LEN];
  size_t pending = 0;
  for (;;) {
    if (!pending) {
      uint8_t request[JW_MAX_FRAME_LEN];
      size_t size = 0;
      if (transport.poll_receive(transport.context, request, sizeof(request), &size) == FRAME_OK) {
        const int result = jw_endpoint_process(&endpoint,
                                               request,
                                               size,
                                               response,
                                               sizeof(response),
                                               JoshuaCommand,
                                               JoshuaReset,
                                               &channel);
        if (result > 0) pending = (size_t)result;
      }
    }
    if (pending && transport.try_send(transport.context, response, pending) == FRAME_OK)
      pending = 0;
    JoshuaService(&channel, ClockP_getTimeUsec());
    ClockP_usleep(1000u);
  }
}

int32_t JoshuaSerialStart(void) {
  if (!JoshuaBootId(&boot_id)) return SystemP_FAILURE;
  TaskP_Params parameters;
  TaskP_Params_init(&parameters);
  parameters.name = "joshua_serial";
  parameters.stackSize = sizeof(task_stack);
  parameters.stack = task_stack;
  parameters.priority = JOSHUA_SERIAL_TASK_PRIORITY;
  parameters.taskMain = JoshuaSerialTask;
  parameters.args = NULL;
  return TaskP_construct(&task_object, &parameters);
}
void JoshuaSerialDisableLogs(void) {
  (void)DebugP_logZoneDisable(0xFFFFFFFFU);
}
void JoshuaSerialDiscardLog(void* context, const char* format, va_list args) {
  (void)context;
  (void)format;
  (void)args;
}
