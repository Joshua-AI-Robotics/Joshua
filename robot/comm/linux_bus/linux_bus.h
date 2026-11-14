#pragma once
#include <memory>
#include <string>

#include "robot/comm/interfaces/i2c_transport.h"
#include "robot/comm/interfaces/spi_transport.h"
#include "robot/comm/proto/comm.pb.h"
namespace robot::comm::linux_bus {
class System {
 public:
  virtual ~System() = default;
  virtual int Open(const std::string& path) = 0;
  virtual void Close(int fd) = 0;
  virtual int Lock(int fd) = 0;
  virtual int Ioctl(int fd, unsigned long command, void* data) = 0;
  virtual absl::StatusOr<std::string> Identity(int fd) = 0;
  virtual absl::StatusOr<std::string> Controller(int fd, bool spi) = 0;
  virtual absl::StatusOr<uint32_t> ChipSelect(int fd) = 0;
};
absl::StatusOr<std::shared_ptr<SpiTransport>> OpenSpi(const SpiConfig& config,
                                                      std::shared_ptr<System> system = {});
absl::StatusOr<std::shared_ptr<I2cTransport>> OpenI2c(const I2cConfig& config,
                                                      std::shared_ptr<System> system = {});
// Terminal shutdown invalidates retained endpoints; ordinary last release closes.
void ShutdownAll();
}  // namespace robot::comm::linux_bus
