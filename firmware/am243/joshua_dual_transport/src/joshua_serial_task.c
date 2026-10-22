// UART0 endpoint for the current correlated JoshuaWire protocol. The adjacent
// TI EtherCAT demo has independent, vendor-defined PDO state and no GPIO output.
#include "joshua_serial_task.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>
#include <security/crypto/pka/hw_include/cslr_cp_ace.h>
#include <security/crypto/rng/rng.h>
#include <security/crypto/sa2ul/sa2ul.h>
#include <stdbool.h>
#include <stddef.h>

#include "joshua_commands.h"
#include "joshua_wire_endpoint.h"
#include "joshua_wire_stream.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"

#define JOSHUA_SERIAL_TASK_STACK_SIZE 4096U
#define JOSHUA_SERIAL_TASK_PRIORITY (TaskP_PRIORITY_HIGHEST - 4U)
#define JOSHUA_SERIAL_BYTE_TIMEOUT_US 20000U

static uint8_t task_stack[JOSHUA_SERIAL_TASK_STACK_SIZE] __attribute__((aligned(32)));
static TaskP_Object task_object;
static JoshuaChannel channel;
static jw_endpoint_t endpoint;
static jw_stream_decoder_t decoder;

static bool BootId(uint32_t* boot) {
  uint32_t entropy[4];
  SA2UL_engineEnable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
  RNG_Handle rng = RNG_open(0);
  if (!rng) {
    SA2UL_engineDisable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
    return false;
  }
  bool ok = RNG_setup(rng) == RNG_RETURN_SUCCESS && RNG_read(rng, entropy) == RNG_RETURN_SUCCESS;
  RNG_close(rng);
  SA2UL_engineDisable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
  if (ok) *boot = entropy[0] ? entropy[0] : entropy[1];
  return ok && *boot != 0;
}

static bool UartRead(uint8_t* byte) {
  UART_Transaction transaction;
  UART_Transaction_init(&transaction);
  transaction.buf = byte;
  transaction.count = 1;
  transaction.timeout = ClockP_usecToTicks(JOSHUA_SERIAL_BYTE_TIMEOUT_US);
  return UART_read(gUartHandle[CONFIG_UART_CONSOLE], &transaction) == SystemP_SUCCESS &&
         transaction.count == 1;
}
static bool UartWrite(const uint8_t* bytes, size_t size) {
  UART_Transaction transaction;
  UART_Transaction_init(&transaction);
  transaction.buf = (void*)bytes;
  transaction.count = size;
  transaction.timeout = ClockP_usecToTicks(JOSHUA_SERIAL_BYTE_TIMEOUT_US);
  return UART_write(gUartHandle[CONFIG_UART_CONSOLE], &transaction) == SystemP_SUCCESS;
}
static void JoshuaSerialTask(void* args) {
  (void)args;
  for (;;) {
    channel.feedback.metadata.timestamp_us = ClockP_getTimeUsec();
    ++channel.feedback.metadata.sequence;
    uint8_t byte;
    if (!UartRead(&byte)) continue;
    int size = jw_stream_feed(&decoder, byte, (uint32_t)(ClockP_getTimeUsec() / 1000U));
    if (size <= 0) continue;
    uint8_t response[JW_MAX_FRAME_LEN];
    const int result = jw_endpoint_process(&endpoint,
                                           decoder.bytes,
                                           size,
                                           response,
                                           sizeof(response),
                                           JoshuaCommand,
                                           JoshuaReset,
                                           &channel);
    if (result > 0) (void)UartWrite(response, result);
  }
}
int32_t JoshuaSerialStart(void) {
  uint32_t boot;
  if (!BootId(&boot)) return SystemP_FAILURE;
  JoshuaInit(&channel, boot, JW_PLANE_MESSAGE);
  jw_endpoint_init(&endpoint);
  jw_stream_init(&decoder);
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
