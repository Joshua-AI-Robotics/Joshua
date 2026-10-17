// SDK callbacks copy whole images into the portable profile. One R5 core owns
// the stack; short interrupt-disabled sections serialize its callbacks with an
// independent watchdog task. FIQ handlers and other cores must not enter this
// owner. No EtherCAT stack API, sleep or physical I/O inside the critical section.
// A stop epoch rejects snapshots admitted before a stop while SDK I/O was running.
#include "joshua_ethercat_ti.h"

#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/TaskP.h>
#include <string.h>

#include "am243_software_backend.h"
#include "ecSlvApiDef.h"
#include "joshua_ethercat_profile.h"

#if !defined(JOSHUA_COMM_WATCHDOG_US) || !defined(JOSHUA_TARGET_WATCHDOG_US)
#error "Explicit nonzero watchdog limits are required for the JW EtherCAT artifact"
#endif

static JoshuaEthercatProfile profile;
static JoshuaChannel channel;
static TaskP_Object watchdog_task;
static uint8_t watchdog_stack[4096] __attribute__((aligned(32)));
static uint64_t stop_epoch;  // Read/written only with this core's IRQs disabled.

static uint64_t AdmissionEpoch(void) {
  const uintptr_t key = HwiP_disable();
  const uint64_t epoch = stop_epoch;
  HwiP_restore(key);
  return epoch;
}
// Profile callbacks run under the same owner critical section as dispatch.
static void StopLocked(void* context, uint16_t faults) {
  ++stop_epoch;
  JoshuaStop(context, faults);
}
static int CommandLocked(void* context, const jw_command_t* command, uint8_t* out, size_t cap) {
  const int length = JoshuaCommand(context, command, out, cap);
  jw_reply_t reply;
  if (command->cmd == JW_CMD_ESTOP && length > 0 &&
      jw_decode_reply_payload(out, (size_t)length, &reply) == JW_RESULT_OK &&
      reply.status == JW_STATUS_OK && reply.execution == JW_EXECUTION_COMPLETE)
    ++stop_epoch;
  return length;
}

static void TickLocked(bool op) {
  const uint64_t now = ClockP_getTimeUsec();
  JoshuaService(&channel, now);
  JoshuaEthercatProfileTick(&profile, now, op);
}
static void Watchdog(void* ignored) {
  (void)ignored;
  for (;;) {
    const uintptr_t key = HwiP_disable();
    TickLocked(profile.operational);
    HwiP_restore(key);
    ClockP_usleep(1000);  // Detection allowance: interval + RTOS scheduling delay.
  }
}
static void StopOutput(void* ignored) {
  (void)ignored;
  const uintptr_t key = HwiP_disable();
  profile.operational = false;
  // Latch even when disabled: an ENABLE/RECOVER may be waiting in SDK I/O.
  JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_STATE);
  HwiP_restore(key);
}
static uint8_t ReadObject(void* ignored,
                          uint16_t index,
                          uint8_t subindex,
                          uint32_t length,
                          uint16_t* data,
                          uint8_t complete) {
  (void)ignored;
  if (subindex || complete) return ABORTIDX_UNSUPPORTED_ACCESS;
  const uint64_t epoch = AdmissionEpoch();
  const bool op = EC_API_SLV_getState() == EC_API_SLV_eESM_op;
  const uintptr_t key = HwiP_disable();
  TickLocked(epoch == stop_epoch ? op : profile.operational);
  const int result =
      epoch == stop_epoch ? JoshuaEthercatProfileRead(&profile, index, (uint8_t*)data, length) : -1;
  HwiP_restore(key);
  return result == 0 ? 0 : ABORTIDX_UNSUPPORTED_ACCESS;
}
static uint8_t WriteObject(void* ignored,
                           uint16_t index,
                           uint8_t subindex,
                           uint32_t length,
                           uint16_t* data,
                           uint8_t complete) {
  (void)ignored;
  if (subindex || complete) return ABORTIDX_UNSUPPORTED_ACCESS;
  const uint64_t epoch = AdmissionEpoch();
  const bool op = EC_API_SLV_getState() == EC_API_SLV_eESM_op;
  const uintptr_t key = HwiP_disable();
  TickLocked(epoch == stop_epoch ? op : profile.operational);
  const int result = epoch == stop_epoch
                         ? JoshuaEthercatProfileWrite(&profile, index, (const uint8_t*)data, length)
                         : -1;
  HwiP_restore(key);
  return result == 0 ? 0 : ABORTIDX_UNSUPPORTED_ACCESS;
}
static uint32_t AddObject(EC_API_SLV_SHandle_t* slave,
                          uint16_t index,
                          char* name,
                          uint16_t type,
                          uint16_t size,
                          uint16_t access) {
  return EC_API_SLV_CoE_odAddVariable(
      slave, index, name, type, size * 8, access, ReadObject, NULL, WriteObject, NULL);
}
static uint32_t AddPdo(EC_API_SLV_SHandle_t* slave, bool output) {
  const uint16_t object_index = output ? 0x7000 : 0x6000;
  const uint16_t pdo_index = output ? 0x1600 : 0x1a00;
  // The callbacks reject these indices: command images are PDO-only, not an
  // alternate CoE path around the session/management object contract.
  uint32_t result = EC_API_SLV_CoE_odAddArray(
      slave,
      object_index,
      output ? "JW output image" : "JW input image",
      JWEC_PDO_SIZE / 4,
      DEFTYPE_UNSIGNED32,
      32,
      output ? ACCESS_WRITE | OBJACCESS_RXPDOMAPPING : ACCESS_READ | OBJACCESS_TXPDOMAPPING,
      ReadObject,
      NULL,
      WriteObject,
      NULL);
  if (result) return result;
  EC_API_SLV_Pdo_t* pdo = NULL;
  result = EC_API_SLV_PDO_create(slave, output ? "JW RxPDO" : "JW TxPDO", pdo_index, &pdo);
  if (result) return result;
  for (uint8_t sub = 1; sub <= JWEC_PDO_SIZE / 4; ++sub) {
    EC_API_SLV_SCoE_ObjEntry_t* entry = NULL;
    result = EC_API_SLV_CoE_getObjectEntry(slave, object_index, sub, &entry);
    if (result) return result;
    result = EC_API_SLV_PDO_createEntry(slave, pdo, "JW image word", entry);
    if (result) return result;
  }
  return EC_API_SLV_PDO_setFixed(slave, pdo, true);
}
uint32_t JoshuaEthercatConfigure(EC_API_SLV_SHandle_t* slave) {
  JoshuaInit(&channel, (uint32_t)ClockP_getTimeUsec() | 1u, JW_PLANE_MESSAGE | JW_PLANE_CYCLIC);
  const JoshuaEthercatProfileConfig config = {.identity = channel.runtime.identity,
                                              .artifact = "am243-ec-jw",
                                              .context = &channel,
                                              .command = CommandLocked,
                                              .reset = JoshuaReset,
                                              .stop = StopLocked,
                                              .enabled = JoshuaEnabled};
  if (!slave || JoshuaEthercatProfileInit(
                    &profile, &config, JOSHUA_COMM_WATCHDOG_US, JOSHUA_TARGET_WATCHDOG_US))
    return EC_API_eERR_INVALID;
  // Distinct evaluation profile, not a registered commercial product identity.
  uint32_t result = EC_API_SLV_setProductCode(slave, 0x4a570002);
  if (result) return result;
  result = EC_API_SLV_setRevisionNumber(slave, 0x00020001);
  if (result) return result;
  result = EC_API_SLV_setProductName(slave, "Joshua AM243 JW software channel");
  if (result) return result;
  result = AddObject(
      slave, JWEC_DESCRIPTOR_INDEX, "JWEC descriptor", DEFTYPE_OCTETSTRING, 36, ACCESS_READ);
  if (result) return result;
  result = AddObject(
      slave, JWEC_SESSION_INDEX, "JWEC reset session", DEFTYPE_OCTETSTRING, 8, ACCESS_READWRITE);
  if (result) return result;
  result = AddObject(slave,
                     JWEC_REQUEST_INDEX,
                     "JWEC request",
                     DEFTYPE_OCTETSTRING,
                     JWEC_MAILBOX_SIZE,
                     ACCESS_WRITE);
  if (result) return result;
  result = AddObject(slave,
                     JWEC_RESPONSE_INDEX,
                     "JWEC response",
                     DEFTYPE_OCTETSTRING,
                     JWEC_MAILBOX_SIZE,
                     ACCESS_READ);
  if (result) return result;
  result =
      AddObject(slave, JWEC_ACK_INDEX, "JWEC response ack", DEFTYPE_UNSIGNED32, 4, ACCESS_WRITE);
  if (result) return result;
  result = AddPdo(slave, true);
  if (result) return result;
  result = AddPdo(slave, false);
  if (result) return result;
  result = EC_API_SLV_PDO_setAssignment(slave, false);
  if (result) return result;
  EC_API_SLV_cbRegisterStopOuputHandler(slave, NULL, StopOutput);
  TaskP_Params params;
  TaskP_Params_init(&params);
  params.name = "jw_watchdog";
  params.stack = watchdog_stack;
  params.stackSize = sizeof(watchdog_stack);
  // SDK 09's application/stack task runs at HIGHEST-1. Safety must preempt it
  // even if a vendor call stalls; only one bounded owner critical section delays it.
  params.priority = TaskP_PRIORITY_HIGHEST;
  params.taskMain = Watchdog;
  return TaskP_construct(&watchdog_task, &params) == SystemP_SUCCESS ? 0 : EC_API_eERR_INVALID;
}
void JoshuaEthercatRun(EC_API_SLV_SHandle_t* slave) {
  const uint64_t epoch = AdmissionEpoch();
  const bool op = EC_API_SLV_getState() == EC_API_SLV_eESM_op;
  uint8_t output[JWEC_PDO_SIZE];
  uint8_t input[JWEC_PDO_SIZE];
  const bool mapped = EC_API_SLV_getOutputProcDataLength(slave) == JWEC_PDO_SIZE * 8 &&
                      EC_API_SLV_getInputProcDataLength(slave) == JWEC_PDO_SIZE * 8;
  uint32_t result = 0;
  if (op && mapped) result = EC_API_SLV_getOutputData(slave, sizeof(output), output);
  const uintptr_t key = HwiP_disable();
  TickLocked(epoch == stop_epoch ? op && mapped : profile.operational);
  if (result)
    JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_STATE);
  else if (epoch == stop_epoch && op && mapped)
    (void)JoshuaEthercatProfilePdo(&profile, output, sizeof(output));
  memcpy(input, profile.input, sizeof(input));
  HwiP_restore(key);
  if (mapped && EC_API_SLV_setInputData(slave, sizeof(input), input)) {
    const uintptr_t fault_key = HwiP_disable();
    JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_STATE);
    HwiP_restore(fault_key);
  }
}
