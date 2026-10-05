// AM243-LP SOES startup and ESC HAL. Uses TI's external ICSS FWHAL/PRU firmware,
// never the EC_API_SLV evaluation stack. The channel is software-only: this port
// is for communication qualification, not physical motor control.
#include <FreeRTOS.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <string.h>
#include <task.h>
#include <tiescsoc.h>

#include "esc.h"
#include "esc_eep.h"
#include "firmware/common/soes/joshua_ethercat_soes.h"
#include "joshua_commands.h"
#include "ti_board_open_close.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"

#if !defined(JOSHUA_COMM_WATCHDOG_US) || !defined(JOSHUA_TARGET_WATCHDOG_US)
#error "Explicit watchdog budgets are required"
#endif

PRUICSS_Handle pruIcss1Handle;
uint8_t* pEEPROM;
extern const unsigned char tiesc_eeprom[];  // External SDK's ESC boot configuration.
static JoshuaEthercatProfile profile;
static JoshuaChannel channel = {.latch_estop = true};
static uint8_t sii[TIESC_EEPROM_SIZE];
static StackType_t main_stack[4096] __attribute__((aligned(32)));
static StackType_t watchdog_stack[1024] __attribute__((aligned(32)));
static StaticTask_t main_task, watchdog_task;

// Bench identity only. A shipped product needs an authorized EtherCAT identity.
#define JOSHUA_SII_VENDOR 0xe000059du
#define JOSHUA_SII_PRODUCT 0x4a570002u
#define JOSHUA_SII_REVISION 0x00020002u

static uintptr_t Lock(void) {
  return HwiP_disable();
}
static void Unlock(uintptr_t key) {
  HwiP_restore(key);
}
static void Fault(void) {
  const uintptr_t key = Lock();
  JoshuaEthercatProfileFault(&profile, JOSHUA_ECAT_FAULT_STATE);
  Unlock(key);
}
static void Watchdog(void* ignored) {
  (void)ignored;
  for (;;) {
    const uintptr_t key = Lock();
    JoshuaEthercatProfileTick(&profile, ClockP_getTimeUsec(), profile.operational);
    Unlock(key);
    ClockP_usleep(1000);
  }
}
static void Put16(size_t offset, uint16_t v) {
  sii[offset] = (uint8_t)v;
  sii[offset + 1] = (uint8_t)(v >> 8);
}
static void Put32(size_t offset, uint32_t v) {
  Put16(offset, (uint16_t)v);
  Put16(offset + 2, (uint16_t)(v >> 16));
}
static void BuildSii(void) {
  memset(sii, 0, sizeof(sii));
  // Preserve only TI's ESC hardware settings and their CRC, not demo identity
  // or PDO mapping. Nothing is written to physical EEPROM or flash.
  memcpy(sii, tiesc_eeprom, 16);
  Put32(16, JOSHUA_SII_VENDOR);
  Put32(20, JOSHUA_SII_PRODUCT);
  Put32(24, JOSHUA_SII_REVISION);
  Put16(48, MBX0_sma);
  Put16(50, MBXSIZE);
  Put16(52, MBX1_sma);
  Put16(54, MBXSIZE);
  Put16(56, 0x0004);  // CoE only, no boot/FoE/EoE.
  Put16(124, sizeof(sii) / 128 - 1);
  Put16(126, 1);
  // FMMU category: outputs, inputs, mailbox status, unused.
  Put16(128, 40);
  Put16(130, 2);
  sii[132] = 1;
  sii[133] = 2;
  sii[134] = 3;
  // Four fixed SM records. PDO entries are discoverable through CoE.
  Put16(136, 41);
  Put16(138, 16);
  const uint16_t start[] = {MBX0_sma, MBX1_sma, SM2_sma, SM3_sma};
  const uint16_t length[] = {MBXSIZE, MBXSIZE, JWEC_PDO_SIZE, JWEC_PDO_SIZE};
  const uint8_t control[] = {0x26, 0x22, 0x24, 0x20};
  for (size_t i = 0; i < 4; ++i) {
    const size_t offset = 140 + 8 * i;
    Put16(offset, start[i]);
    Put16(offset + 2, length[i]);
    sii[offset + 4] = control[i];
    sii[offset + 6] = 1;  // Enable
    sii[offset + 7] = (uint8_t)(i + 1);
  }
  // General category: SDO + SDO information, no Complete Access/dynamic mapping;
  // MII ports 0/1 and split LRD/LWR, matching the ICSS ESC and JW contract.
  Put16(172, 30);
  Put16(174, 16);
  sii[179] = 1;  // Device name is string 1 below.
  sii[180] = 0x05;
  sii[181] = 0x03;
  sii[187] = 0x02;
  static const char name[] = "Joshua AM243 JW SOES";
  const size_t string_words = (sizeof(name) + 2) / 2;
  Put16(208, 10);
  Put16(210, string_words);
  sii[212] = 1;
  sii[213] = sizeof(name) - 1;
  memcpy(sii + 214, name, sizeof(name) - 1);
  Put16(212 + 2 * string_words, 0xffff);
}
static int32_t NoStoredSii(uint8_t* data, uint32_t size) {
  (void)data;
  (void)size;
  return SystemP_FAILURE;  // Force this artifact's compiled SII into RAM.
}
static int32_t NoPersistentWrite(uint8_t* data, uint32_t size) {
  (void)data;
  (void)size;
  return SystemP_FAILURE;
}
void EEP_init(void) {}
int8_t EEP_read(uint32_t address, uint8_t* data, uint16_t size) {
  if (address > sizeof(sii) || size > sizeof(sii) - address) return 1;
  memcpy(data, bsp_get_eeprom_cache_base() + address, size);
  return 0;
}
int8_t EEP_write(uint32_t address, uint8_t* data, uint16_t size) {
  (void)address;
  (void)data;
  (void)size;
  return 1;  // Fixed, read-only identity/mapping; no runtime EEPROM changes.
}
static void Reload(eep_stat_t* status) {
  if (bsp_eeprom_emulation_reload(pruIcss1Handle) != 0) {
    status->contstat.bits.csumErr = 1;
    status->contstat.bits.ackErr = 1;
  }
}
static void RefreshEvents(void) {
  CC_ATOMIC_SET(ESCvar.ALevent, bsp_read_word_isr(pruIcss1Handle, ESCREG_ALEVENT));
}
void ESC_read(uint16_t address, void* data, uint16_t size) {
  if (address == SM2_sma) {
    int16_t sm;
    const uint16_t actual = bsp_get_process_data_address(pruIcss1Handle, address, size, &sm);
    if (actual < ESC_ADDR_MEMORY) {
      memset(data, 0, size);
      Fault();
    } else {
      bsp_read(pruIcss1Handle, data, actual, size);
      bsp_process_data_access_complete(pruIcss1Handle, address, size, sm);
    }
  } else if (address == MBX0_sma) {
    bsp_pdi_mbx_read_start(pruIcss1Handle);
    bsp_read(pruIcss1Handle, data, address, size);
    if (size >= MBXSIZE - 2) bsp_pdi_mbx_read_complete(pruIcss1Handle);
  } else {
    bsp_read(pruIcss1Handle, data, address, size);
    bsp_pdi_post_read_indication(pruIcss1Handle, address, size);
    if (address + size == MBX0_sma + MBXSIZE) bsp_pdi_mbx_read_complete(pruIcss1Handle);
  }
  RefreshEvents();
}
void ESC_write(uint16_t address, void* data, uint16_t size) {
  if (address == SM3_sma) {
    int16_t sm;
    const uint16_t actual = bsp_get_process_data_address(pruIcss1Handle, address, size, &sm);
    if (actual < ESC_ADDR_MEMORY) {
      Fault();
    } else {
      bsp_write(pruIcss1Handle, data, actual, size);
      bsp_process_data_access_complete(pruIcss1Handle, address, size, sm);
    }
  } else if (address >= MBX1_sma && address < MBX1_sma + MBXSIZE) {
    if (address == MBX1_sma) bsp_pdi_mbx_write_start(pruIcss1Handle);
    bsp_write(pruIcss1Handle, data, address, size);
    if (address + size == MBX1_sma + MBXSIZE) bsp_pdi_mbx_write_complete(pruIcss1Handle);
  } else {
    uint16_t value = 0;
    if (size) value = ((const uint8_t*)data)[0];
    if (size > 1) value |= (uint16_t)((const uint8_t*)data)[1] << 8;
    bsp_write(pruIcss1Handle, data, address, size);
    bsp_pdi_write_indication(pruIcss1Handle, address, size, value);
  }
  RefreshEvents();
}
void ESC_init(const esc_cfg_t* config) {
  (void)config;
  bsp_set_sm_properties(pruIcss1Handle, 0, MBX0_sma, MBXSIZE);
  bsp_set_sm_properties(pruIcss1Handle, 1, MBX1_sma, MBXSIZE);
  bsp_set_sm_properties(pruIcss1Handle, 2, SM2_sma, 3 * JWEC_PDO_SIZE);
  bsp_set_sm_properties(pruIcss1Handle, 3, SM3_sma, 3 * JWEC_PDO_SIZE);
  bsp_write_dword(pruIcss1Handle, 0, ESCREG_ALEVENTMASK);
}

static void Run(void* ignored) {
  (void)ignored;
  Drivers_open();
  DebugP_assert(Board_driversOpen() == SystemP_SUCCESS);
  const JoshuaEthercatProfileConfig config = {.identity = {.board_id = JW_BOARD_AM243,
                                                           .fw_name = "am243-soes-v2",
                                                           .n_channels = 1,
                                                           .channel_drives = {JW_DRIVE_STEP_DIR}},
                                              .artifact = "am243-soes2",
                                              .context = &channel,
                                              .command = JoshuaCommand,
                                              .reset = JoshuaReset,
                                              .stop = JoshuaStop,
                                              .enabled = JoshuaEnabled};
  DebugP_assert(JoshuaEthercatProfileInit(
                    &profile, &config, JOSHUA_COMM_WATCHDOG_US, JOSHUA_TARGET_WATCHDOG_US) == 0);
  DebugP_assert(xTaskCreateStatic(Watchdog,
                                  "jw_watchdog",
                                  1024,
                                  NULL,
                                  configMAX_PRIORITIES - 2,
                                  watchdog_stack,
                                  &watchdog_task) != NULL);
  tiesc_socEvmInit();
  DebugP_assert(tiesc_isEthercatDevice());
#ifdef MDIO_MANUAL_MODE_ENABLED
  tiesc_mdioManualModeSetup();
#endif
  tiesc_ethphyEnablePowerDown();
  BuildSii();
  bsp_params params;
  tiesc_socParamsInit(&params);
  params.default_tiesc_eeprom = sii;
  params.eeprom_read = NoStoredSii;
  params.eeprom_write = NoPersistentWrite;
  DebugP_assert(bsp_init(&params) == SystemP_SUCCESS);
  // Command acknowledgments require the FWHAL IRQ even though SOES is polled.
  bsp_start_esc_isr(pruIcss1Handle);
  EEP_set_reload_function_pointer(Reload);
  EEP_set_read_size(8);
  const uint64_t deadline = ClockP_getTimeUsec() + 2000000;
  while (!(bsp_read_word_isr(pruIcss1Handle, ESCREG_DLSTATUS) & 1)) {
    DebugP_assert(ClockP_getTimeUsec() < deadline);
    ClockP_usleep(1000);
  }
  const JoshuaSoesConfig binding = {.profile = &profile,
                                    .vendor_id = JOSHUA_SII_VENDOR,
                                    .product_code = JOSHUA_SII_PRODUCT,
                                    .revision = JOSHUA_SII_REVISION,
                                    .time_us = ClockP_getTimeUsec,
                                    .lock = Lock,
                                    .unlock = Unlock,
                                    .eeprom_handler = EEP_process};
  DebugP_assert(JoshuaSoesInit(&binding) == 0);
  tiesc_ethphyDisablePowerDown();
  DebugP_log("Joshua JW SOES ready; software-only channel, fixed SII, no TI slave stack\r\n");
  uint64_t next_log = ClockP_getTimeUsec() + 60000000;
  for (;;) {
    JoshuaSoesPoll();
    if (ClockP_getTimeUsec() >= next_log) {
      DebugP_log("JW SOES uptime=%u s AL=0x%02x error=0x%04x\r\n",
                 (unsigned)(ClockP_getTimeUsec() / 1000000),
                 ESCvar.ALstatus,
                 ESCvar.ALerror);
      next_log = ClockP_getTimeUsec() + 60000000;
    }
    ClockP_usleep(1000);  // Initial qualification cadence, not a 1 kHz timing guarantee.
  }
}
int main(void) {
  System_init();
  Board_init();
  DebugP_assert(
      xTaskCreateStatic(
          Run, "jw_soes", 4096, NULL, configMAX_PRIORITIES - 3, main_stack, &main_task) != NULL);
  vTaskStartScheduler();
  DebugP_assertNoLog(0);
  return 0;
}
