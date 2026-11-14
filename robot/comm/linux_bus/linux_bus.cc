#include "robot/comm/linux_bus/linux_bus.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>

#include "robot/comm/linux_bus/bus_validation.h"
#include "utils/status_macros.h"
namespace robot::comm::linux_bus {
namespace {
absl::Status Error(const std::string& what) {
  const int error = errno;
  auto code = error == EBUSY || error == EWOULDBLOCK ? absl::StatusCode::kAlreadyExists
              : error == EACCES || error == EPERM    ? absl::StatusCode::kPermissionDenied
                                                     : absl::StatusCode::kUnavailable;
  return absl::Status(code, what + ": " + std::strerror(error));
}
class NativeSystem final : public System {
 public:
  int Open(const std::string& path) override {
    return open(path.c_str(), O_RDWR | O_CLOEXEC);
  }
  void Close(int fd) override {
    close(fd);
  }
  int Lock(int fd) override {
    return flock(fd, LOCK_EX | LOCK_NB);
  }
  int Ioctl(int fd, unsigned long op, void* data) override {
    return ioctl(fd, op, data);
  }
  absl::StatusOr<std::string> Identity(int fd) override {
    struct stat info {};
    if (fstat(fd, &info) < 0) return Error("Inspect bus device");
    if (!S_ISCHR(info.st_mode))
      return absl::InvalidArgumentError("Bus selector is not a character device");
    return std::to_string(major(info.st_rdev)) + ":" + std::to_string(minor(info.st_rdev));
  }
  absl::StatusOr<uint32_t> ChipSelect(int fd) override {
    ABSL_ASSIGN_OR_RETURN(auto id, Identity(fd));
    std::ifstream input("/sys/dev/char/" + id + "/device/of_node/reg", std::ios::binary);
    unsigned char bytes[4];
    if (!input.read(reinterpret_cast<char*>(bytes), 4))
      return absl::FailedPreconditionError("SPI device has no chip-select reg identity");
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
           bytes[3];
  }
  absl::StatusOr<std::string> Controller(int fd, bool spi) override {
    ABSL_ASSIGN_OR_RETURN(auto id, Identity(fd));
    std::error_code error;
    auto node = std::filesystem::canonical("/sys/dev/char/" + id + "/device/of_node", error);
    if (error)
      return absl::FailedPreconditionError("Bus device has no resolvable device-tree identity");
    if (spi) node = node.parent_path();
    return node.filename().string();
  }
};
std::shared_ptr<System> DefaultSystem() {
  static auto s = std::make_shared<NativeSystem>();
  return s;
}
struct Owner {
  std::shared_ptr<System> system;
  int fd = -1;
  bool closed = false;
  bool fault = false;
  std::string policy;
  std::mutex mutex;
  ~Owner() {
    Close();
  }
  void Close() {
    std::lock_guard lock(mutex);
    if (fd >= 0) system->Close(fd);
    fd = -1;
    closed = true;
  }
  absl::Status Ready() const {
    return closed || fault ? absl::FailedPreconditionError(
                                 "Bus endpoint is closed/faulted; release and reconstruct")
                           : absl::OkStatus();
  }
  absl::Status Fail(const std::string& context) {
    fault = true;
    return Error(context + "; outcome may be unknown; no automatic retry");
  }
};
auto& CacheMutex() {
  static auto* m = new std::mutex;
  return *m;
}
auto& Owners() {
  static auto* map = new std::map<std::string, std::weak_ptr<Owner>>;
  return *map;
}
absl::StatusOr<std::shared_ptr<Owner>> Acquire(const std::string& device,
                                               const std::string& controller,
                                               bool spi,
                                               const std::string& policy,
                                               std::shared_ptr<System> system) {
  if (!system) system = DefaultSystem();
  auto owner = std::make_shared<Owner>();
  owner->system = system;
  owner->fd = system->Open(device);
  if (owner->fd < 0) return Error("Open " + device);
  ABSL_ASSIGN_OR_RETURN(auto id, system->Identity(owner->fd));
  ABSL_ASSIGN_OR_RETURN(auto actual, system->Controller(owner->fd, spi));
  if (actual != controller)
    return absl::FailedPreconditionError("Bus controller identity differs from config: " + actual);
  const auto key = std::to_string(reinterpret_cast<uintptr_t>(system.get())) + ":" + id;
  std::lock_guard lock(CacheMutex());
  auto found = Owners().find(key);
  if (found != Owners().end())
    if (auto existing = found->second.lock()) {
      if (spi || existing->policy != policy)
        return absl::AlreadyExistsError("Bus endpoint already has an owner or incompatible policy");
      std::lock_guard existing_lock(existing->mutex);
      ABSL_RETURN_IF_ERROR(existing->Ready());
      return existing;
    }
  if (system->Lock(owner->fd) < 0) return Error("Exclusive advisory bus ownership");
  owner->policy = policy;
  Owners()[key] = owner;
  return owner;
}
class Spi final : public SpiTransport {
 public:
  Spi(std::shared_ptr<Owner> owner, uint32_t speed) : owner_(std::move(owner)), speed_(speed) {}
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& bytes) override {
    if (bytes.empty() || bytes.size() > 32)
      return absl::InvalidArgumentError("SPI transfer length must be 1..32");
    std::lock_guard lock(owner_->mutex);
    ABSL_RETURN_IF_ERROR(owner_->Ready());
    std::vector<uint8_t> result(bytes.size());
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<uintptr_t>(bytes.data());
    transfer.rx_buf = reinterpret_cast<uintptr_t>(result.data());
    transfer.len = bytes.size();
    transfer.speed_hz = speed_;
    transfer.bits_per_word = 8;
    const int n = owner_->system->Ioctl(owner_->fd, SPI_IOC_MESSAGE(1), &transfer);
    if (n < 0) return owner_->Fail("SPI transfer");
    if (static_cast<size_t>(n) != bytes.size()) {
      owner_->fault = true;
      return absl::DataLossError("Short SPI transfer; outcome unknown");
    }
    return result;
  }

 private:
  std::shared_ptr<Owner> owner_;
  uint32_t speed_;
};
class I2c final : public I2cTransport {
 public:
  I2c(std::shared_ptr<Owner> owner, uint16_t address)
      : owner_(std::move(owner)), address_(address) {}
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& write,
                                                size_t read_count) override {
    if (write.size() + read_count == 0 || write.size() + read_count > 32)
      return absl::InvalidArgumentError("I2C combined transfer must be 1..32 bytes");
    std::lock_guard lock(owner_->mutex);
    ABSL_RETURN_IF_ERROR(owner_->Ready());
    if (owner_->system->Ioctl(owner_->fd, I2C_SLAVE, reinterpret_cast<void*>(uintptr_t(address_))) <
        0)
      return owner_->Fail("I2C target may be owned by a kernel driver");
    std::vector<uint8_t> result(read_count);
    i2c_msg messages[2]{};
    uint32_t count = 0;
    if (!write.empty())
      messages[count++] = {
          address_, 0, static_cast<__u16>(write.size()), const_cast<__u8*>(write.data())};
    if (read_count)
      messages[count++] = {address_, I2C_M_RD, static_cast<__u16>(read_count), result.data()};
    i2c_rdwr_ioctl_data transfer{messages, count};
    const int n = owner_->system->Ioctl(owner_->fd, I2C_RDWR, &transfer);
    if (n < 0) return owner_->Fail("I2C repeated-start transfer");
    if (n != static_cast<int>(count)) {
      owner_->fault = true;
      return absl::DataLossError("Short I2C transfer; outcome unknown");
    }
    return result;
  }

 private:
  std::shared_ptr<Owner> owner_;
  uint16_t address_;
};
}  // namespace
absl::StatusOr<std::shared_ptr<SpiTransport>> OpenSpi(const SpiConfig& c,
                                                      std::shared_ptr<System> system) {
  ABSL_RETURN_IF_ERROR(ValidateSpiSettings(c));
  ABSL_ASSIGN_OR_RETURN(auto owner,
                        Acquire(c.device(), c.controller(), true, "spi", std::move(system)));
  std::lock_guard lock(owner->mutex);
  ABSL_ASSIGN_OR_RETURN(auto chip_select, owner->system->ChipSelect(owner->fd));
  if (chip_select != c.chip_select_index())
    return absl::FailedPreconditionError("SPI device chip-select identity mismatch");
  uint32_t mode = c.mode();
  uint8_t bits = 8;
  uint32_t speed = c.speed_hz();
  if (owner->system->Ioctl(owner->fd, SPI_IOC_WR_MODE32, &mode) < 0 ||
      owner->system->Ioctl(owner->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      owner->system->Ioctl(owner->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    return owner->Fail("Configure SPI");
  uint32_t mode_read = ~mode;
  uint8_t bits_read = 0;
  if (owner->system->Ioctl(owner->fd, SPI_IOC_RD_MODE32, &mode_read) < 0 ||
      owner->system->Ioctl(owner->fd, SPI_IOC_RD_BITS_PER_WORD, &bits_read) < 0)
    return owner->Fail("Verify SPI settings");
  if (mode_read != mode || bits_read != 8)
    return absl::FailedPreconditionError("SPI driver did not accept mode/word size");
  return std::shared_ptr<SpiTransport>(new Spi(std::move(owner), speed));
}
absl::StatusOr<std::shared_ptr<I2cTransport>> OpenI2c(const I2cConfig& c,
                                                      std::shared_ptr<System> system) {
  ABSL_RETURN_IF_ERROR(ValidateI2cSettings(c));
  const auto policy = "i2c:" + std::to_string(c.timeout_ms());
  ABSL_ASSIGN_OR_RETURN(auto owner,
                        Acquire(c.device(), c.controller(), false, policy, std::move(system)));
  std::lock_guard lock(owner->mutex);
  unsigned long capabilities = 0;
  if (owner->system->Ioctl(owner->fd, I2C_FUNCS, &capabilities) < 0)
    return owner->Fail("Inspect I2C capabilities");
  if (!(capabilities & I2C_FUNC_I2C))
    return absl::UnimplementedError("Adapter does not support combined I2C transactions");
  if (owner->system->Ioctl(owner->fd,
                           I2C_TIMEOUT,
                           reinterpret_cast<void*>(uintptr_t((c.timeout_ms() + 9) / 10))) < 0 ||
      owner->system->Ioctl(owner->fd, I2C_SLAVE, reinterpret_cast<void*>(uintptr_t(c.address()))) <
          0)
    return owner->Fail("Configure I2C target/timeout");
  return std::shared_ptr<I2cTransport>(new I2c(std::move(owner), c.address()));
}
void ShutdownAll() {
  std::vector<std::shared_ptr<Owner>> owners;
  {
    std::lock_guard lock(CacheMutex());
    for (auto& [key, weak] : Owners())
      if (auto owner = weak.lock()) owners.push_back(std::move(owner));
    Owners().clear();
  }
  for (auto& owner : owners) owner->Close();
}
}  // namespace robot::comm::linux_bus
