// Fixed CoE dictionary and PDO callbacks for JW on SOES. ESC access and board
// startup are supplied elsewhere; all command semantics stay in the profile.
#include "joshua_ethercat_soes.h"

#include <string.h>

#include "ecat_slv.h"
#include "esc_coe.h"

CC_STATIC_ASSERT(MAX_RXPDO_SIZE == JWEC_PDO_SIZE && MAX_TXPDO_SIZE == JWEC_PDO_SIZE,
                 "SOES options must match the JW PDO contract");

static JoshuaSoesConfig port;
static uint32_t identity[4];
static uint8_t descriptor[JWEC_DESCRIPTOR_SIZE], session[JWEC_SESSION_SIZE];
static uint8_t request[JWEC_MAILBOX_SIZE], response[JWEC_MAILBOX_SIZE], ack[4];
static uint8_t output[JWEC_PDO_SIZE], input[JWEC_PDO_SIZE];

#define ENTRY(s, t, bits, access, value, data) \
  { s, t, bits, access, "", value, data }
#define COUNT(n) ENTRY(0, DTYPE_UNSIGNED8, 8, ATYPE_RO, n, NULL)
#define CONST(s, t, bits, v) ENTRY(s, t, bits, ATYPE_RO, v, NULL)
#define WORDS(M)                                                                                  \
  M(1), M(2), M(3), M(4), M(5), M(6), M(7), M(8), M(9), M(10), M(11), M(12), M(13), M(14), M(15), \
      M(16), M(17), M(18), M(19), M(20)
#define RXMAP(s) CONST(s, DTYPE_UNSIGNED32, 32, 0x70000020u + ((s) << 8))
#define TXMAP(s) CONST(s, DTYPE_UNSIGNED32, 32, 0x60000020u + ((s) << 8))
#define RXWORD(s) ENTRY(s, DTYPE_UNSIGNED32, 32, ATYPE_WO | ATYPE_RXPDO, 0, output + 4 * ((s) - 1))
#define TXWORD(s) ENTRY(s, DTYPE_UNSIGNED32, 32, ATYPE_RO | ATYPE_TXPDO, 0, input + 4 * ((s) - 1))
static const _objd device_type[] = {CONST(0, DTYPE_UNSIGNED32, 32, 0)};
static const _objd device_name[] = {
    ENTRY(0, DTYPE_VISIBLE_STRING, 9 * 8, ATYPE_RO, 0, "Joshua JW")};
static const _objd device_identity[] = {COUNT(4),
                                        ENTRY(1, DTYPE_UNSIGNED32, 32, ATYPE_RO, 0, &identity[0]),
                                        ENTRY(2, DTYPE_UNSIGNED32, 32, ATYPE_RO, 0, &identity[1]),
                                        ENTRY(3, DTYPE_UNSIGNED32, 32, ATYPE_RO, 0, &identity[2]),
                                        ENTRY(4, DTYPE_UNSIGNED32, 32, ATYPE_RO, 0, &identity[3])};
static const _objd rxmap[] = {COUNT(20), WORDS(RXMAP)};
static const _objd txmap[] = {COUNT(20), WORDS(TXMAP)};
static const _objd sm_types[] = {COUNT(4),
                                 CONST(1, DTYPE_UNSIGNED8, 8, 1),
                                 CONST(2, DTYPE_UNSIGNED8, 8, 2),
                                 CONST(3, DTYPE_UNSIGNED8, 8, 3),
                                 CONST(4, DTYPE_UNSIGNED8, 8, 4)};
static const _objd rxassign[] = {COUNT(1), CONST(1, DTYPE_UNSIGNED16, 16, 0x1600)};
static const _objd txassign[] = {COUNT(1), CONST(1, DTYPE_UNSIGNED16, 16, 0x1a00)};
#define OCTETS(name, access) \
  static const _objd od_##name[] = {ENTRY(0, DTYPE_OCTET_STRING, sizeof(name) * 8, access, 0, name)}
OCTETS(descriptor, ATYPE_RO);
OCTETS(session, ATYPE_RW);
OCTETS(request, ATYPE_WO);
OCTETS(response, ATYPE_RO);
static const _objd od_ack[] = {ENTRY(0, DTYPE_UNSIGNED32, 32, ATYPE_WO, 0, ack)};
static const _objd rxwords[] = {COUNT(20), WORDS(RXWORD)};
static const _objd txwords[] = {COUNT(20), WORDS(TXWORD)};
#define OBJECT(i, type, n, label, d) \
  { i, type, n, 0, label, d }
const _objectlist SDOobjects[] = {
    OBJECT(0x1000, OTYPE_VAR, 0, "Device type", device_type),
    OBJECT(0x1008, OTYPE_VAR, 0, "Device name", device_name),
    OBJECT(0x1018, OTYPE_RECORD, 4, "Identity", device_identity),
    OBJECT(0x1600, OTYPE_RECORD, 20, "JW RxPDO", rxmap),
    OBJECT(0x1a00, OTYPE_RECORD, 20, "JW TxPDO", txmap),
    OBJECT(0x1c00, OTYPE_ARRAY, 4, "Sync managers", sm_types),
    OBJECT(0x1c12, OTYPE_ARRAY, 1, "Rx assignment", rxassign),
    OBJECT(0x1c13, OTYPE_ARRAY, 1, "Tx assignment", txassign),
    OBJECT(JWEC_DESCRIPTOR_INDEX, OTYPE_VAR, 0, "JWEC descriptor", od_descriptor),
    OBJECT(JWEC_SESSION_INDEX, OTYPE_VAR, 0, "JWEC reset", od_session),
    OBJECT(JWEC_REQUEST_INDEX, OTYPE_VAR, 0, "JWEC request", od_request),
    OBJECT(JWEC_RESPONSE_INDEX, OTYPE_VAR, 0, "JWEC response", od_response),
    OBJECT(JWEC_ACK_INDEX, OTYPE_VAR, 0, "JWEC ack", od_ack),
    OBJECT(0x6000, OTYPE_ARRAY, 20, "JW input image", txwords),
    OBJECT(0x7000, OTYPE_ARRAY, 20, "JW output image", rxwords),
    OBJECT(0xffff, 0, 0, NULL, NULL)};

static void TickLocked(void) {
  JoshuaEthercatProfileTick(port.profile, port.time_us(), ESCvar.ALstatus == ESCop);
}
static void Tick(void) {
  const uintptr_t key = port.lock();
  TickLocked();
  port.unlock(key);
}
static void Stop(void) {
  const uintptr_t key = port.lock();
  JoshuaEthercatProfileTick(port.profile, port.time_us(), false);
  port.unlock(key);
}
static uint8_t* Writable(uint16_t index, size_t* size) {
  switch (index) {
    case JWEC_SESSION_INDEX:
      *size = sizeof(session);
      return session;
    case JWEC_REQUEST_INDEX:
      *size = sizeof(request);
      return request;
    case JWEC_ACK_INDEX:
      *size = sizeof(ack);
      return ack;
    default:
      *size = 0;
      return NULL;
  }
}
static uint32_t BeforeDownload(
    uint16_t index, uint8_t sub, void* data, size_t size, uint16_t flags) {
  (void)data;
  if (flags & COMPLETE_ACCESS_FLAG) return ABORT_CA_NOT_SUPPORTED;
  size_t expected;
  if (sub || !Writable(index, &expected)) return ABORT_UNSUPPORTED;
  if (size != expected) return ABORT_TYPEMISMATCH;
  // Every JW object fits one 512-byte mailbox. Reject partial/segmented writes
  // before SOES copies any data, including a declared length with high bits set
  // (the upstream parser truncates the normal-download length to 16 bits).
  const _COEsdo* sdo = (const _COEsdo*)MBX;
  const bool expedited = (sdo->command & COE_EXPEDITED_INDICATOR) != 0;
  const size_t wire_size = COE_HEADERSIZE + (expedited ? 0 : expected);
  if (sdo->mbxheader.length != wire_size || (!expedited && sdo->size != expected))
    return ABORT_TYPEMISMATCH;
  return 0;
}
static uint32_t AfterDownload(uint16_t index, uint8_t sub, uint16_t flags) {
  size_t size;
  uint8_t* data = Writable(index, &size);
  if (!data || sub || (flags & COMPLETE_ACCESS_FLAG)) return ABORT_UNSUPPORTED;
  // Called only after SOES has copied the validated complete transfer.
  const uintptr_t key = port.lock();
  TickLocked();
  const int result = JoshuaEthercatProfileWrite(port.profile, index, data, size);
  port.unlock(key);
  return result == 0 ? 0 : ABORT_UNSUPPORTED;
}
static uint32_t BeforeUpload(
    uint16_t index, uint8_t sub, void* data, size_t* size, uint16_t flags) {
  if (flags & COMPLETE_ACCESS_FLAG) return ABORT_CA_NOT_SUPPORTED;
  // Discovery objects are readable; PDO words are never an alternate CoE plane.
  if (index < JWEC_DESCRIPTOR_INDEX) return 0;
  if (sub || (index != JWEC_DESCRIPTOR_INDEX && index != JWEC_SESSION_INDEX &&
              index != JWEC_RESPONSE_INDEX))
    return ABORT_UNSUPPORTED;
  const uintptr_t key = port.lock();
  TickLocked();
  const int result = JoshuaEthercatProfileRead(port.profile, index, data, *size);
  port.unlock(key);
  return result == 0 ? 0 : ABORT_UNSUPPORTED;
}
void cb_set_outputs(void) {
  const uintptr_t key = port.lock();
  TickLocked();
  (void)JoshuaEthercatProfilePdo(port.profile, output, sizeof(output));
  port.unlock(key);
}
void cb_get_inputs(void) {
  const uintptr_t key = port.lock();
  TickLocked();
  memcpy(input, port.profile->input, sizeof(input));
  port.unlock(key);
}
int JoshuaSoesInit(const JoshuaSoesConfig* config) {
  if (!config || !config->profile || !config->time_us || !config->lock || !config->unlock)
    return -1;
  port = *config;
  identity[0] = config->vendor_id;
  identity[1] = config->product_code;
  identity[2] = config->revision;
  identity[3] = config->serial;
  esc_cfg_t stack = {.use_interrupt = 0,
                     .skip_default_initialization = true,
                     .application_hook = Tick,
                     .safeoutput_override = Stop,
                     .pre_object_download_hook = BeforeDownload,
                     .post_object_download_hook = AfterDownload,
                     .pre_object_upload_hook = BeforeUpload,
                     .esc_hw_eep_handler = config->eeprom_handler};
  ecat_slv_init(&stack);
  return 0;
}
void JoshuaSoesPoll(void) {
  ecat_slv_poll();
  // Watchdog policy uses real monotonic time in the shared profile, not SOES's
  // loop-count watchdog. The independent board watchdog remains mandatory.
  DIG_process(DIG_PROCESS_OUTPUTS_FLAG | DIG_PROCESS_APP_HOOK_FLAG | DIG_PROCESS_INPUTS_FLAG);
}
