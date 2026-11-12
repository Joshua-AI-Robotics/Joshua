#include "robot/board/host_gpio/host_peripherals.h"

#include "gtest/gtest.h"
namespace robot::board {
namespace {
class PwmFake final : public PwmOutput {
 public:
  bool enabled = false, closed = false;
  double duty = 0;
  absl::Status SetDutyCycle(double d) override {
    duty = d;
    return absl::OkStatus();
  }
  absl::Status Enable() override {
    enabled = true;
    return absl::OkStatus();
  }
  absl::Status Disable() override {
    enabled = false;
    return absl::OkStatus();
  }
  absl::Status Close() override {
    closed = true;
    enabled = false;
    return absl::OkStatus();
  }
};
class SpiFake final : public robot::comm::SpiTransport {
 public:
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& v) override {
    return v;
  }
};
class I2cFake final : public robot::comm::I2cTransport {
 public:
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>&, size_t n) override {
    return std::vector<uint8_t>(n, 42);
  }
};
class UartFake final : public robot::comm::ByteStream {
 public:
  std::vector<uint8_t> bytes;
  absl::Status Open() override {
    return absl::OkStatus();
  }
  absl::Status Write(const std::vector<uint8_t>& v) override {
    bytes = v;
    return absl::OkStatus();
  }
  absl::StatusOr<std::vector<uint8_t>> Read(size_t) override {
    return bytes;
  }
};
class Backend final : public PeripheralBackend {
 public:
  std::shared_ptr<PwmFake> pwm = std::make_shared<PwmFake>();
  int calls = 0;
  bool fail_spi = false;
  absl::StatusOr<std::shared_ptr<PwmOutput>> Pwm(const PeripheralResource&) override {
    ++calls;
    return pwm;
  }
  absl::StatusOr<std::shared_ptr<robot::comm::SpiTransport>> Spi(
      const PeripheralResource&) override {
    ++calls;
    if (fail_spi) return absl::UnavailableError("injected SPI failure");
    return std::make_shared<SpiFake>();
  }
  absl::StatusOr<std::shared_ptr<robot::comm::I2cTransport>> I2c(
      const PeripheralResource&) override {
    ++calls;
    return std::make_shared<I2cFake>();
  }
  absl::StatusOr<std::shared_ptr<robot::comm::ByteStream>> Uart(
      const PeripheralResource&) override {
    ++calls;
    return std::make_shared<UartFake>();
  }
};
Board Config() {
  Board b;
  b.set_name("host");
  b.set_board_type(HOST_GPIO);
  b.mutable_host_gpio_config()->set_profile_id("raspberrypi/pi5");
  b.mutable_host_gpio_config()->set_profile_version(1);
  auto* p = b.add_peripherals();
  p->set_name("pwm");
  p->set_purpose("fan");
  p->mutable_pwm()->mutable_pin()->set_header_pin(32);
  p->mutable_pwm()->set_frequency_hz(1000);
  p->mutable_pwm()->set_initial_duty_cycle(0);
  p = b.add_peripherals();
  p->set_name("spi");
  p->set_purpose("ADC");
  auto* s = p->mutable_spi();
  s->mutable_clock()->set_header_pin(23);
  s->mutable_mosi()->set_header_pin(19);
  s->mutable_miso()->set_header_pin(21);
  s->mutable_chip_select()->set_header_pin(24);
  s->mutable_settings()->set_device("/dev/spidev0.0");
  s->mutable_settings()->set_controller("spi@50000");
  s->mutable_settings()->set_chip_select_index(0);
  s->mutable_settings()->set_speed_hz(1000000);
  p = b.add_peripherals();
  p->set_name("i2c");
  p->set_purpose("temperature");
  auto* i = p->mutable_i2c();
  i->mutable_sda()->set_header_pin(3);
  i->mutable_scl()->set_header_pin(5);
  i->mutable_settings()->set_device("/dev/i2c-1");
  i->mutable_settings()->set_controller("i2c@74000");
  i->mutable_settings()->set_address(0x48);
  i->mutable_settings()->set_timeout_ms(20);
  p = b.add_peripherals();
  p->set_name("uart");
  p->set_purpose("GPS");
  auto* u = p->mutable_uart();
  u->mutable_tx()->set_header_pin(8);
  u->mutable_rx()->set_header_pin(10);
  u->set_controller("serial@30000");
  u->mutable_settings()->set_port("/dev/ttyAMA0");
  u->mutable_settings()->set_baudrate(115200);
  return b;
}
TEST(HostPeripheralsTest, ExposesTypedCapabilitiesAndInvalidatesAllRetainedHandles) {
  auto backend = std::make_shared<Backend>();
  auto group = HostPeripherals::Open(Config(), backend);
  ASSERT_TRUE(group.ok()) << group.status();
  auto p = (*group)->Get<PwmOutput>("pwm");
  auto s = (*group)->Get<robot::comm::SpiTransport>("spi");
  auto i = (*group)->Get<robot::comm::I2cTransport>("i2c");
  auto u = (*group)->Get<robot::comm::ByteStream>("uart");
  ASSERT_TRUE(p.ok());
  ASSERT_TRUE(s.ok());
  ASSERT_TRUE(i.ok());
  ASSERT_TRUE(u.ok());
  EXPECT_FALSE((*group)->Get<PwmOutput>("spi").ok());
  EXPECT_FALSE((*group)->Get<PwmOutput>("missing").ok());
  EXPECT_TRUE((*p)->Enable().ok());
  EXPECT_TRUE(backend->pwm->enabled);
  EXPECT_EQ((*s)->Transfer({1, 2}).value(), (std::vector<uint8_t>{1, 2}));
  EXPECT_EQ((*i)->Transfer({1}, 2).value(), (std::vector<uint8_t>{42, 42}));
  EXPECT_TRUE((*u)->Write({3, 4}).ok());
  EXPECT_EQ((*u)->Read(2).value(), (std::vector<uint8_t>{3, 4}));
  EXPECT_FALSE((*u)->Read(33).ok());
  ASSERT_TRUE((*group)->Close().ok());
  EXPECT_TRUE(backend->pwm->closed);
  EXPECT_FALSE(backend->pwm->enabled);
  EXPECT_FALSE((*p)->Enable().ok());
  EXPECT_FALSE((*s)->Transfer({1}).ok());
  EXPECT_FALSE((*i)->Transfer({}, 1).ok());
  EXPECT_FALSE((*u)->Open().ok());
  EXPECT_TRUE((*group)->Close().ok());
}
TEST(HostPeripheralsTest, ValidatesBeforeIoAndRollsBackEarlierAcquisitions) {
  auto backend = std::make_shared<Backend>();
  auto b = Config();
  b.mutable_peripherals(3)->mutable_uart()->mutable_tx()->set_header_pin(1);
  EXPECT_FALSE(HostPeripherals::Open(b, backend).ok());
  EXPECT_EQ(backend->calls, 0);
  backend->fail_spi = true;
  EXPECT_FALSE(HostPeripherals::Open(Config(), backend).ok());
  EXPECT_EQ(backend->calls, 2);
  EXPECT_TRUE(backend->pwm->closed);
}
}  // namespace
}  // namespace robot::board
