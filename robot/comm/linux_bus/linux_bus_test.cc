#include "robot/comm/linux_bus/linux_bus.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>

#include <cerrno>
#include <cstring>
#include <set>

#include "gtest/gtest.h"
namespace robot::comm::linux_bus {
namespace {
class Fake final : public System {
 public:
  int next = 10, messages = 0;
  bool fail = false, short_transfer = false, locked = false;
  uint32_t mode = 0, select = 0;
  std::set<int> open;
  std::string controller = "spi@50000";
  int Open(const std::string&) override {
    open.insert(next);
    return next++;
  }
  void Close(int fd) override {
    EXPECT_EQ(open.erase(fd), 1);
  }
  int Lock(int) override {
    if (locked) {
      errno = EBUSY;
      return -1;
    }
    return 0;
  }
  absl::StatusOr<std::string> Identity(int) override {
    return "device";
  }
  absl::StatusOr<std::string> Controller(int, bool) override {
    return controller;
  }
  absl::StatusOr<uint32_t> ChipSelect(int) override {
    return select;
  }
  int Ioctl(int, unsigned long op, void* p) override {
    if (op == SPI_IOC_WR_MODE32)
      mode = *static_cast<uint32_t*>(p);
    else if (op == SPI_IOC_RD_MODE32)
      *static_cast<uint32_t*>(p) = mode;
    else if (op == SPI_IOC_RD_BITS_PER_WORD)
      *static_cast<uint8_t*>(p) = 8;
    else if (op == SPI_IOC_MESSAGE(1)) {
      ++messages;
      if (fail) {
        errno = EIO;
        return -1;
      }
      auto* t = static_cast<spi_ioc_transfer*>(p);
      std::memcpy(
          reinterpret_cast<void*>(t->rx_buf), reinterpret_cast<const void*>(t->tx_buf), t->len);
      return short_transfer ? t->len - 1 : t->len;
    } else if (op == I2C_FUNCS)
      *static_cast<unsigned long*>(p) = I2C_FUNC_I2C;
    else if (op == I2C_RDWR) {
      ++messages;
      if (fail) {
        errno = EIO;
        return -1;
      }
      auto* t = static_cast<i2c_rdwr_ioctl_data*>(p);
      for (uint32_t i = 0; i < t->nmsgs; ++i)
        if (t->msgs[i].flags & I2C_M_RD) std::memset(t->msgs[i].buf, 0x5a, t->msgs[i].len);
      return short_transfer ? t->nmsgs - 1 : t->nmsgs;
    }
    return 0;
  }
};
SpiConfig SpiConfigForTest() {
  SpiConfig c;
  c.set_device("/dev/spidev0.0");
  c.set_controller("spi@50000");
  c.set_speed_hz(1000000);
  c.set_chip_select_index(0);
  return c;
}
I2cConfig I2cConfigForTest() {
  I2cConfig c;
  c.set_device("/dev/i2c-1");
  c.set_controller("i2c@74000");
  c.set_address(0x40);
  c.set_timeout_ms(10);
  return c;
}
TEST(LinuxBusTest, SpiFullDuplexBoundsIdentityAndFaultAreEnforced) {
  auto io = std::make_shared<Fake>();
  auto bus = OpenSpi(SpiConfigForTest(), io);
  ASSERT_TRUE(bus.ok()) << bus.status();
  EXPECT_EQ(*(*bus)->Transfer({1, 2, 3}), (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_FALSE((*bus)->Transfer({}).ok());
  EXPECT_FALSE((*bus)->Transfer(std::vector<uint8_t>(33)).ok());
  auto duplicate = OpenSpi(SpiConfigForTest(), io);
  EXPECT_FALSE(duplicate.ok());
  io->fail = true;
  EXPECT_FALSE((*bus)->Transfer({1}).ok());
  io->fail = false;
  const auto count = io->messages;
  EXPECT_FALSE((*bus)->Transfer({1}).ok());
  EXPECT_EQ(io->messages, count);
  ShutdownAll();
  EXPECT_FALSE((*bus)->Transfer({1}).ok());
  EXPECT_TRUE(io->open.empty());
}
TEST(LinuxBusTest, I2cRepeatedStartSharesOwnerAndChecksShortCompletion) {
  auto io = std::make_shared<Fake>();
  io->controller = "i2c@74000";
  auto config = I2cConfigForTest();
  auto first = OpenI2c(config, io);
  ASSERT_TRUE(first.ok()) << first.status();
  config.set_address(0x41);
  auto second = OpenI2c(config, io);
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(io->open.size(), 1);
  EXPECT_EQ(*(*first)->Transfer({0x10}, 3), (std::vector<uint8_t>{0x5a, 0x5a, 0x5a}));
  EXPECT_FALSE((*first)->Transfer(std::vector<uint8_t>(32), 1).ok());
  io->short_transfer = true;
  EXPECT_FALSE((*second)->Transfer({}, 1).ok());
  EXPECT_FALSE((*first)->Transfer({}, 1).ok());
  ShutdownAll();
  EXPECT_TRUE(io->open.empty());
}
TEST(LinuxBusTest, ControllerChipSelectAndAdvisoryOwnershipFailuresDoNotLeakDescriptors) {
  auto io = std::make_shared<Fake>();
  io->controller = "wrong";
  EXPECT_FALSE(OpenSpi(SpiConfigForTest(), io).ok());
  EXPECT_TRUE(io->open.empty());
  io->controller = "spi@50000";
  io->select = 1;
  EXPECT_FALSE(OpenSpi(SpiConfigForTest(), io).ok());
  EXPECT_TRUE(io->open.empty());
  io->select = 0;
  io->locked = true;
  EXPECT_FALSE(OpenSpi(SpiConfigForTest(), io).ok());
  EXPECT_TRUE(io->open.empty());
}
}  // namespace
}  // namespace robot::comm::linux_bus
