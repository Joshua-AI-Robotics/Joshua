// Run the production TI bridge with a native SDK substitute. IRQ exclusion is
// modeled by a mutex; SDK calls can suspend while the real stop/watchdog callback
// runs. This proves ordering/state behavior, not R5 execution time or IRQ latency.
#include "joshua_ethercat_ti.h"

#include <ecSlvApiDef.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/TaskP.h>

#include <atomic>
#include <csetjmp>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "am243_software_backend.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_ethercat.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;
std::recursive_mutex owner;
thread_local uintptr_t irq_depth;
thread_local std::jmp_buf* watchdog_yield;
std::atomic<uint64_t> now_us;
TaskP_Params watchdog;
EC_API_SLV_CBObjRead_t read_object;
EC_API_SLV_CBObjWrite_t write_object;
EC_API_SLV_CBStopOutputHandler_t stop_output;
std::mutex hook_mutex;
std::function<void()> on_state, on_output, on_input;
Bytes output_image, input_image;

void SdkCall() {
  EXPECT_EQ(irq_depth, 0u) << "Vendor I/O must not prevent the watchdog from running";
}
void RunHook(std::function<void()>& slot) {
  std::function<void()> hook;
  {
    std::lock_guard<std::mutex> lock(hook_mutex);
    hook = std::exchange(slot, {});
  }
  if (hook) hook();
}
void RunWatchdogOnce() {
  // The production task loops forever. The fake sleep yields after one complete
  // iteration, with IRQ exclusion already released. No C++ objects needing
  // destruction are skipped between setjmp and the fake sleep's longjmp.
  std::jmp_buf yield;
  watchdog_yield = &yield;
  if (setjmp(yield) == 0) watchdog.taskMain(watchdog.args);
  watchdog_yield = nullptr;
}
void Put32(uint8_t* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = value >> (8 * i);
}
uint32_t U32(const uint8_t* in) {
  return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) |
         (uint32_t(in[3]) << 24);
}

class EthercatTiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(irq_depth, 0u);
    now_us = 100;
    on_state = on_output = on_input = {};
    read_object = nullptr;
    write_object = nullptr;
    stop_output = nullptr;
    watchdog = {};
    output_image.assign(JWEC_PDO_SIZE, 0);
    input_image.clear();
    ASSERT_EQ(JoshuaEthercatConfigure(&slave), 0u);
    ASSERT_NE(read_object, nullptr);
    ASSERT_NE(write_object, nullptr);
    ASSERT_NE(stop_output, nullptr);
    Reset(7);
  }
  uint8_t Write(uint16_t index, const Bytes& data) {
    std::vector<uint16_t> aligned((data.size() + 1) / 2);
    std::memcpy(aligned.data(), data.data(), data.size());
    return write_object(nullptr, index, 0, data.size(), aligned.data(), 0);
  }
  Bytes Read(uint16_t index, size_t size) {
    std::vector<uint16_t> aligned((size + 1) / 2);
    EXPECT_EQ(read_object(nullptr, index, 0, size, aligned.data(), 0), 0);
    const auto* bytes = reinterpret_cast<const uint8_t*>(aligned.data());
    return Bytes(bytes, bytes + size);
  }
  void Reset(uint32_t session) {
    Bytes data(JWEC_SESSION_SIZE);
    Put32(data.data(), JWEC_RESET_OPERATION);
    Put32(data.data() + 4, session);
    ASSERT_EQ(Write(JWEC_SESSION_INDEX, data), 0);
    session_id = session;
    message_id = 1;
    generation = 0;
  }
  Bytes Image(uint8_t command, const Bytes& payload, uint8_t function, bool pdo = false) {
    Bytes image(pdo ? JWEC_PDO_SIZE : JWEC_MAILBOX_SIZE);
    Put32(image.data(), session_id);
    Put32(image.data() + 4, ++generation);
    const size_t offset = pdo ? JWEC_PDO_FRAME_OFFSET : JWEC_MAILBOX_FRAME_OFFSET;
    const int size = jw_encode_frame(image.data() + offset,
                                     JW_MAX_FRAME_LEN,
                                     session_id,
                                     message_id++,
                                     command,
                                     function,
                                     payload.data(),
                                     payload.size());
    EXPECT_GT(size, 0);
    image[offset - 4] = size;
    return image;
  }
  Bytes Operation(uint8_t operation, const Bytes& data = {}) {
    Bytes encoded(JW_MAX_PAYLOAD_LEN);
    const jw_profile_request_t request{
        JW_PROFILE_ACTUATOR, 1, 0, operation, data.data(), data.size()};
    const int size = jw_encode_profile_request(encoded.data(), encoded.size(), &request);
    EXPECT_GT(size, 0);
    encoded.resize(size);
    return encoded;
  }
  Bytes Management(uint8_t command, const Bytes& payload = {}, uint8_t function = JW_CHANNEL_NONE) {
    const Bytes request = Image(command, payload, function);
    EXPECT_EQ(Write(JWEC_REQUEST_INDEX, request), 0);
    const Bytes response = Read(JWEC_RESPONSE_INDEX, JWEC_MAILBOX_SIZE);
    jw_frame_t frame;
    if (jw_decode_frame(response.data() + 12, response[8], &frame) != JW_RESULT_OK) {
      ADD_FAILURE() << "Missing firmware response";
      return {};
    }
    Bytes reply(frame.payload, frame.payload + frame.payload_len);
    EXPECT_EQ(Write(JWEC_ACK_INDEX, Bytes(request.begin() + 4, request.begin() + 8)), 0);
    return reply;
  }
  void Configure() {
    Bytes encoded(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
    jw_configure_step_dir_t config{};
    config.max_pulse_rate_hz = 1000;
    ASSERT_GT(jw_encode_configure_step_dir_payload(encoded.data(), encoded.size(), &config), 0);
    EXPECT_EQ(Management(JW_CMD_PROFILE, Operation(JW_ACTUATOR_CONFIGURE, encoded), 0),
              (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  }
  void Enable() {
    Configure();
    EXPECT_EQ(Management(JW_CMD_PROFILE, Operation(JW_ACTUATOR_ENABLE), 0),
              (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
    EXPECT_EQ(State(0).state, JW_STATE_ACTIVE);
  }
  jw_device_status_t State(uint8_t function = JW_CHANNEL_NONE) {
    const Bytes reply = Management(JW_CMD_GET_STATUS, {}, function);
    jw_reply_t common{};
    jw_device_status_t status{};
    if (jw_decode_reply_payload(reply.data(), reply.size(), &common) != JW_RESULT_OK ||
        jw_decode_device_status(common.data, common.data_len, &status) != JW_RESULT_OK)
      ADD_FAILURE() << "Missing board status";
    return status;
  }
  void ExpectStopped() {
    const auto state = State();
    EXPECT_EQ(state.state, JW_STATE_UNAVAILABLE);
    EXPECT_EQ(state.stop_phase, JW_STOP_ACTION_COMPLETE);
    EXPECT_TRUE(state.recovery & JW_RECOVERY_CLEAR_ESTOP);
    EXPECT_EQ(Management(JW_CMD_PROFILE, Operation(JW_ACTUATOR_ENABLE), 0),
              (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  }
  EC_API_SLV_SHandle_t slave{};
  uint32_t session_id = 7, message_id = 1, generation = 0;
};

TEST_F(EthercatTiTest, SafetyTaskHasPriorityAboveSdkApplication) {
  // SDK 09's 401 application uses HIGHEST-1. Exercise its actual task registration.
  EXPECT_GT(watchdog.priority, TaskP_PRIORITY_HIGHEST - 1);
  EXPECT_EQ(watchdog.stackSize, 4096u);
  RunWatchdogOnce();
  EXPECT_EQ(irq_depth, 0u);
}

TEST_F(EthercatTiTest, StopWhileDisabledInvalidatesAnAdmittedEnable) {
  Configure();
  const Bytes enable = Image(JW_CMD_PROFILE, Operation(JW_ACTUATOR_ENABLE), 0);
  on_state = [] { stop_output(nullptr); };
  EXPECT_EQ(Write(JWEC_REQUEST_INDEX, enable), ABORTIDX_UNSUPPORTED_ACCESS);
  ExpectStopped();
  Reset(8);
  ExpectStopped();
  EXPECT_EQ(Management(JW_CMD_RECOVER, Bytes(2)), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(Management(JW_CMD_PROFILE, Operation(JW_ACTUATOR_ENABLE), 0),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  Enable();  // Recovery alone cannot restore configuration or enable.
}

TEST_F(EthercatTiTest, RepeatedStopRetiresRecoveryAlreadyWaitingInSdk) {
  Enable();
  stop_output(nullptr);
  const Bytes recover = Image(JW_CMD_RECOVER, Bytes(2), JW_CHANNEL_NONE);
  on_state = [] { stop_output(nullptr); };
  EXPECT_EQ(Write(JWEC_REQUEST_INDEX, recover), ABORTIDX_UNSUPPORTED_ACCESS);
  ExpectStopped();
}

TEST_F(EthercatTiTest, ProtocolEstopRetiresAnAdmittedSessionReset) {
  Enable();
  Bytes reset(JWEC_SESSION_SIZE);
  Put32(reset.data(), JWEC_RESET_OPERATION);
  Put32(reset.data() + 4, 8);
  on_state = [this] {
    EXPECT_EQ(Management(JW_CMD_ESTOP, Bytes(2)), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  };
  EXPECT_EQ(Write(JWEC_SESSION_INDEX, reset), ABORTIDX_UNSUPPORTED_ACCESS);
  EXPECT_EQ(U32(Read(JWEC_SESSION_INDEX, JWEC_SESSION_SIZE).data() + 4), session_id);
  ExpectStopped();
}

TEST_F(EthercatTiTest, IndependentWatchdogRetiresRecoveryDuringVendorCall) {
  Enable();
  const Bytes recover = Image(JW_CMD_RECOVER, Bytes(2), JW_CHANNEL_NONE);
  on_state = [] {
    ASSERT_EQ(irq_depth, 0u);
    now_us += JOSHUA_TARGET_WATCHDOG_US;
    std::thread safety(RunWatchdogOnce);
    safety.join();
  };
  EXPECT_EQ(Write(JWEC_REQUEST_INDEX, recover), ABORTIDX_UNSUPPORTED_ACCESS);
  ExpectStopped();
}

TEST_F(EthercatTiTest, StopRetiresPdoSnapshotDelayedInVendorRead) {
  Enable();
  Bytes target(JW_SET_TARGET_PAYLOAD_LEN);
  ASSERT_GT(jw_encode_set_target_payload(target.data(), target.size(), JW_MODE_POSITION, 42), 0);
  output_image = Image(JW_CMD_PROFILE, Operation(JW_ACTUATOR_SET_TARGET, target), 0, true);
  on_output = [] { stop_output(nullptr); };
  JoshuaEthercatRun(&slave);
  ASSERT_EQ(input_image.size(), JWEC_PDO_SIZE);
  EXPECT_EQ(U32(input_image.data() + 8), 0u);  // The old request never reached dispatch.
  ExpectStopped();
}

TEST_F(EthercatTiTest, StopCanRunWhileVendorPublishesInput) {
  Enable();
  output_image = Image(JW_CMD_PROFILE, Operation(JW_ACTUATOR_GET_FEEDBACK), 0, true);
  on_input = [] { stop_output(nullptr); };
  JoshuaEthercatRun(&slave);
  ExpectStopped();
}

TEST_F(EthercatTiTest, ConcurrentReadsStopAndWatchdogUseOneOwner) {
  Enable();
  std::thread reads([] {
    for (int i = 0; i < 100; ++i) {
      uint16_t data[JWEC_DESCRIPTOR_SIZE / 2] = {};
      const uint8_t result = read_object(nullptr, JWEC_DESCRIPTOR_INDEX, 0, sizeof(data), data, 0);
      EXPECT_TRUE(result == 0 || result == ABORTIDX_UNSUPPORTED_ACCESS);
      if (result == 0) {
        EXPECT_EQ(std::memcmp(data, "JWEC", 4), 0);
      }
    }
  });
  std::thread stops([] {
    for (int i = 0; i < 100; ++i) stop_output(nullptr);
  });
  std::thread ticks([] {
    for (int i = 0; i < 100; ++i) RunWatchdogOnce();
  });
  reads.join();
  stops.join();
  ticks.join();
  ExpectStopped();
}

TEST(Am243StopTest, RepeatedFaultsSurviveResetAndRequireExplicitRecovery) {
  JoshuaChannel channel{};
  JoshuaInit(&channel, 123, JW_PLANE_MESSAGE | JW_PLANE_CYCLIC);
  channel.configured = channel.enabled = true;
  channel.target_value = 42;
  JoshuaStop(&channel, 1);
  JoshuaStop(&channel, 2);
  JoshuaReset(&channel);
  EXPECT_FALSE(channel.configured);
  EXPECT_FALSE(JoshuaEnabled(&channel, 0));
  EXPECT_TRUE(channel.estopped);
  EXPECT_EQ(channel.fault_flags, 3);
  EXPECT_EQ(channel.feedback.fault_flags, 3);
  EXPECT_FLOAT_EQ(channel.target_value, 0);
  const uint8_t domain[2] = {};
  const jw_command_t recover{1, 1, JW_CMD_RECOVER, JW_CHANNEL_NONE, domain, sizeof(domain)};
  uint8_t reply[JW_MAX_PAYLOAD_LEN];
  ASSERT_EQ(JoshuaCommand(&channel, &recover, reply, sizeof(reply)), JW_REPLY_HEADER_LEN);
  EXPECT_EQ(reply[0], JW_STATUS_OK);
  EXPECT_FALSE(channel.estopped);
  EXPECT_EQ(channel.fault_flags, 0);
  EXPECT_FALSE(channel.configured);
  EXPECT_FALSE(JoshuaEnabled(&channel, 0));
}
}  // namespace

extern "C" uintptr_t HwiP_disable(void) {
  owner.lock();
  return irq_depth++;
}
extern "C" void HwiP_restore(uintptr_t key) {
  EXPECT_EQ(irq_depth, key + 1);
  --irq_depth;
  owner.unlock();
}
extern "C" uint64_t ClockP_getTimeUsec(void) {
  return now_us;
}
extern "C" void ClockP_usleep(uint32_t usec) {
  EXPECT_EQ(irq_depth, 0u);
  EXPECT_EQ(usec, 1000u);
  // Only the captured worker calls sleep; it has released every critical section.
  std::longjmp(*watchdog_yield, 1);
}
extern "C" void TaskP_Params_init(TaskP_Params* params) {
  *params = {};
}
extern "C" int32_t TaskP_construct(TaskP_Object*, TaskP_Params* params) {
  SdkCall();
  watchdog = *params;
  return SystemP_SUCCESS;
}
extern "C" EC_API_SLV_EEsmState_t EC_API_SLV_getState(void) {
  SdkCall();
  RunHook(on_state);
  return EC_API_SLV_eESM_op;  // Deliberately stale OP even when STOP ran during this call.
}
extern "C" uint32_t EC_API_SLV_getOutputProcDataLength(EC_API_SLV_SHandle_t*) {
  SdkCall();
  return JWEC_PDO_SIZE * 8;
}
extern "C" uint32_t EC_API_SLV_getInputProcDataLength(EC_API_SLV_SHandle_t*) {
  SdkCall();
  return JWEC_PDO_SIZE * 8;
}
extern "C" uint32_t EC_API_SLV_getOutputData(EC_API_SLV_SHandle_t*,
                                             uint32_t length,
                                             uint8_t* data) {
  SdkCall();
  RunHook(on_output);
  EXPECT_EQ(length, output_image.size());
  std::memcpy(data, output_image.data(), length);
  return 0;
}
extern "C" uint32_t EC_API_SLV_setInputData(EC_API_SLV_SHandle_t*, uint32_t length, uint8_t* data) {
  SdkCall();
  RunHook(on_input);
  input_image.assign(data, data + length);
  return 0;
}
extern "C" void EC_API_SLV_cbRegisterStopOuputHandler(EC_API_SLV_SHandle_t*,
                                                      void*,
                                                      EC_API_SLV_CBStopOutputHandler_t callback) {
  SdkCall();
  stop_output = callback;
}
extern "C" uint32_t EC_API_SLV_CoE_odAddVariable(EC_API_SLV_SHandle_t*,
                                                 uint16_t,
                                                 char*,
                                                 uint16_t,
                                                 uint16_t,
                                                 uint16_t,
                                                 EC_API_SLV_CBObjRead_t read,
                                                 void*,
                                                 EC_API_SLV_CBObjWrite_t write,
                                                 void*) {
  SdkCall();
  read_object = read;
  write_object = write;
  return 0;
}
extern "C" uint32_t EC_API_SLV_CoE_odAddArray(EC_API_SLV_SHandle_t*,
                                              uint16_t,
                                              char*,
                                              uint8_t,
                                              uint16_t,
                                              uint16_t,
                                              uint16_t,
                                              EC_API_SLV_CBObjRead_t,
                                              void*,
                                              EC_API_SLV_CBObjWrite_t,
                                              void*) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_setProductCode(EC_API_SLV_SHandle_t*, uint32_t) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_setRevisionNumber(EC_API_SLV_SHandle_t*, uint32_t) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_setProductName(EC_API_SLV_SHandle_t*, char*) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_PDO_create(EC_API_SLV_SHandle_t*,
                                          char*,
                                          uint16_t,
                                          EC_API_SLV_Pdo_t** pdo) {
  SdkCall();
  static EC_API_SLV_Pdo_t fake;
  *pdo = &fake;
  return 0;
}
extern "C" uint32_t EC_API_SLV_CoE_getObjectEntry(EC_API_SLV_SHandle_t*,
                                                  uint16_t,
                                                  uint8_t,
                                                  EC_API_SLV_SCoE_ObjEntry_t** entry) {
  SdkCall();
  static EC_API_SLV_SCoE_ObjEntry_t fake;
  *entry = &fake;
  return 0;
}
extern "C" uint32_t EC_API_SLV_PDO_createEntry(EC_API_SLV_SHandle_t*,
                                               EC_API_SLV_Pdo_t*,
                                               char*,
                                               EC_API_SLV_SCoE_ObjEntry_t*) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_PDO_setFixed(EC_API_SLV_SHandle_t*, EC_API_SLV_Pdo_t*, bool) {
  SdkCall();
  return 0;
}
extern "C" uint32_t EC_API_SLV_PDO_setAssignment(EC_API_SLV_SHandle_t*, bool) {
  SdkCall();
  return 0;
}
