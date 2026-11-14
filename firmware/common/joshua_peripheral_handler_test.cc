#include "firmware/common/joshua_peripheral_handler.h"

#include <array>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"
namespace {
using Bytes = std::vector<uint8_t>;
jw_peripheral_config_t Pwm(unsigned pin = 6, unsigned hz = 1000) {
  return {JW_PERIPHERAL_PWM, 0, {static_cast<uint8_t>(pin), 255, 255, 255}, hz, 32768, 0, 0};
}
Bytes Encode(const jw_peripheral_config_t& c) {
  Bytes v(18);
  EXPECT_EQ(jw_encode_peripheral_config(v.data(), v.size(), &c), 18);
  return v;
}
TEST(PeripheralCodecTest, StrictBoundsRoutesAndSuccessAcknowledgments) {
  const auto c = Pwm();
  auto v = Encode(c);
  jw_peripheral_config_t decoded{};
  EXPECT_EQ(v, (Bytes{1, 0, 6, 255, 255, 255, 0xe8, 3, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(jw_decode_peripheral_config(v.data(), v.size(), &decoded), JW_RESULT_OK);
  for (size_t n = 0; n < 18; ++n)
    EXPECT_NE(jw_decode_peripheral_config(v.data(), n, &decoded), JW_RESULT_OK);
  v[17] = 1;
  EXPECT_NE(jw_decode_peripheral_config(v.data(), v.size(), &decoded), JW_RESULT_OK);
  EXPECT_TRUE(jw_peripheral_route_valid(1, &c));
  auto reserved = Pwm(2);
  EXPECT_FALSE(jw_peripheral_route_valid(2, &reserved));
  auto i2c = jw_peripheral_config_t{JW_PERIPHERAL_I2C, 0, {18, 19, 255, 255}, 100000, 0x48, 50, 0};
  EXPECT_TRUE(jw_peripheral_route_valid(1, &i2c));
  i2c.timeout_ms = 5;
  EXPECT_FALSE(jw_peripheral_route_valid(1, &i2c));
  EXPECT_TRUE(jw_peripheral_reply_valid(
      JW_PERIPHERAL_CONFIGURE, Encode(c).data(), 18, Encode(c).data(), 18));
  const uint8_t q[] = {JW_PERIPHERAL_I2C, 1, 2, 0}, good[] = {2, 42, 43}, short_reply[] = {1, 42};
  EXPECT_TRUE(jw_peripheral_reply_valid(JW_PERIPHERAL_TRANSFER, q, 4, good, 3));
  EXPECT_FALSE(jw_peripheral_reply_valid(JW_PERIPHERAL_TRANSFER, q, 4, short_reply, 2));
}
class PeripheralHandlerTest : public ::testing::Test {
 protected:
  jw_device_runtime_t runtime{};
  jw_runtime_function_t functions[8]{};
  jw_resource_claim_t table[64]{};
  jw_peripheral_group_t group{};
  unsigned applies = 0, releases = 0;
  bool allowed = true, fail = false;
  void SetUp() override {
    jw_resource_table_init(table, 64);
    runtime.context = this;
    runtime.safety = [](void* p, int) {
      jw_peripheral_group_reset(&static_cast<PeripheralHandlerTest*>(p)->group);
    };
    jw_peripheral_ops_t ops{
        this,
        [](void*, const jw_peripheral_config_t* c) {
          return jw_peripheral_route_valid(1, c) ? JW_STATUS_OK : JW_STATUS_INVALID_PAYLOAD;
        },
        [](void* p, unsigned, const jw_peripheral_config_t*) {
          ++static_cast<PeripheralHandlerTest*>(p)->applies;
          return JW_STATUS_OK;
        },
        [](void* p, unsigned, uint8_t op, const uint8_t* q, size_t n, uint8_t* out, size_t* size) {
          if (static_cast<PeripheralHandlerTest*>(p)->fail) return JW_STATUS_ERROR;
          *size = 0;
          if (op == JW_PERIPHERAL_WRITE) {
            memcpy(out, q, n);
            *size = n;
          }
          return JW_STATUS_OK;
        },
        [](void* p, unsigned) {
          ++static_cast<PeripheralHandlerTest*>(p)->releases;
          return JW_STATUS_OK;
        },
        [](void* p) { return static_cast<PeripheralHandlerTest*>(p)->allowed ? 1 : 0; }};
    jw_peripheral_group_init(&group, &runtime, functions, table, 64, ops);
  }
  jw_reply_t Call(unsigned slot, unsigned op, const Bytes& data = {}) {
    const jw_command_t cmd{
        7, ++message, JW_CMD_PROFILE, static_cast<uint8_t>(192 + slot), nullptr, 0};
    const jw_profile_request_t q{
        JW_PROFILE_PERIPHERAL, 1, 0, static_cast<uint8_t>(op), data.data(), data.size()};
    const int n = jw_peripheral_handle(&group.slots[slot], &cmd, &q, out, sizeof(out));
    EXPECT_GT(n, 0);
    jw_reply_t r{};
    EXPECT_EQ(jw_decode_reply_payload(out, n, &r), JW_RESULT_OK);
    return r;
  }
  uint32_t message = 0;
  uint8_t out[81]{};
};
TEST_F(PeripheralHandlerTest, ClaimsAreAtomicAndPwmTimersRequireCompatibleFrequency) {
  ASSERT_EQ(Call(0, 0, Encode(Pwm())).status, JW_STATUS_OK);
  EXPECT_EQ(Call(1, 0, Encode(Pwm())).status, JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(Call(1, 0, Encode(Pwm(7, 2000))).status, JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(applies, 1u);
  EXPECT_EQ(Call(1, 0, Encode(Pwm(7))).status, JW_STATUS_OK);
  EXPECT_EQ(Call(0, 0, Encode(Pwm(8))).status, JW_STATUS_BUSY);
  EXPECT_EQ(Call(0, JW_PERIPHERAL_RELEASE).status, JW_STATUS_OK);
  EXPECT_EQ(Call(0, 0, Encode(Pwm(8))).status, JW_STATUS_OK);
}
TEST_F(PeripheralHandlerTest, BusAdmissionAndFaultUseSharedSafetyAndUnknownOutcomes) {
  allowed = false;
  EXPECT_EQ(Call(0, 0, Encode(Pwm())).status, JW_STATUS_BUSY);
  EXPECT_EQ(applies, 0u);
  allowed = true;
  ASSERT_EQ(Call(0, 0, Encode(Pwm())).status, JW_STATUS_OK);
  EXPECT_EQ(Call(0, JW_PERIPHERAL_WRITE, {1}).status, JW_STATUS_INVALID_PAYLOAD);
  fail = true;
  const auto r = Call(0, JW_PERIPHERAL_ENABLE);
  EXPECT_EQ(r.status, JW_STATUS_OUTCOME_UNKNOWN);
  EXPECT_EQ(r.execution, JW_EXECUTION_UNKNOWN);
  EXPECT_TRUE(runtime.estopped);
  EXPECT_FALSE(group.slots[0].configured);
  EXPECT_EQ(releases, 1u);
  EXPECT_EQ(Call(0, 0, Encode(Pwm())).status, JW_STATUS_UNAVAILABLE);
}
}  // namespace
