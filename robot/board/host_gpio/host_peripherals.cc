#include "robot/board/host_gpio/host_peripherals.h"

#include <utility>

#include "utils/status_macros.h"
namespace robot::board {
namespace {
template <class T>
class Gate {
 public:
  explicit Gate(std::shared_ptr<T> target) : target_(std::move(target)) {}
  absl::Status Close() {
    std::lock_guard lock(mutex_);
    target_.reset();
    return absl::OkStatus();
  }

 protected:
  std::mutex mutex_;
  std::shared_ptr<T> target_;
};
class Spi final : public robot::comm::SpiTransport, public Gate<robot::comm::SpiTransport> {
 public:
  using Gate::Gate;
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& v) override {
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("SPI handle closed");
    return target_->Transfer(v);
  }
};
class I2c final : public robot::comm::I2cTransport, public Gate<robot::comm::I2cTransport> {
 public:
  using Gate::Gate;
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& v, size_t n) override {
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("I2C handle closed");
    return target_->Transfer(v, n);
  }
};
class Uart final : public robot::comm::ByteStream, public Gate<robot::comm::ByteStream> {
 public:
  using Gate::Gate;
  absl::Status Open() override {
    std::lock_guard lock(mutex_);
    return target_ ? absl::OkStatus() : absl::FailedPreconditionError("UART handle closed");
  }
  absl::Status Write(const std::vector<uint8_t>& v) override {
    if (v.empty() || v.size() > 32)
      return absl::InvalidArgumentError("UART operation length must be 1..32");
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("UART handle closed");
    return target_->Write(v);
  }
  absl::StatusOr<std::vector<uint8_t>> Read(size_t n) override {
    if (!n || n > 32) return absl::InvalidArgumentError("UART operation length must be 1..32");
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("UART handle closed");
    return target_->Read(n);
  }
};
class Pwm final : public PwmOutput, public Gate<PwmOutput> {
 public:
  using Gate::Gate;
  absl::Status SetDutyCycle(double v) override {
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("PWM handle closed");
    return target_->SetDutyCycle(v);
  }
  absl::Status Enable() override {
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("PWM handle closed");
    return target_->Enable();
  }
  absl::Status Disable() override {
    std::lock_guard lock(mutex_);
    if (!target_) return absl::FailedPreconditionError("PWM handle closed");
    return target_->Disable();
  }
  absl::Status Close() override {
    std::lock_guard lock(mutex_);
    if (!target_) return status_;
    status_ = target_->Close();
    target_.reset();
    return status_;
  }

 private:
  absl::Status status_;
};
}  // namespace
HostPeripherals::~HostPeripherals() {
  Close().IgnoreError();
}
absl::StatusOr<std::shared_ptr<HostPeripherals>> HostPeripherals::Open(
    const Board& config, std::shared_ptr<PeripheralBackend> backend) {
  ABSL_ASSIGN_OR_RETURN(auto resources, ResolveBoardPeripherals(config));
  if (!backend) return absl::InvalidArgumentError("Peripheral backend is null");
  auto group = std::make_shared<HostPeripherals>();
  for (const auto& r : resources) {
    absl::Status status;
    switch (r.config.operation_case()) {
      case BoardPeripheral::kPwm: {
        auto value = backend->Pwm(r);
        if (!value.ok()) {
          status = value.status();
          break;
        }
        auto wrapped = std::make_shared<Pwm>(std::move(*value));
        group->endpoints_[r.config.name()] = std::static_pointer_cast<PwmOutput>(wrapped);
        group->stoppers_.push_back([wrapped] { return wrapped->Disable(); });
        group->closers_.push_back([wrapped] { return wrapped->Close(); });
        break;
      }
      case BoardPeripheral::kSpi: {
        auto value = backend->Spi(r);
        if (!value.ok()) {
          status = value.status();
          break;
        }
        auto wrapped = std::make_shared<Spi>(std::move(*value));
        group->endpoints_[r.config.name()] =
            std::static_pointer_cast<robot::comm::SpiTransport>(wrapped);
        group->closers_.push_back([wrapped] { return wrapped->Close(); });
        break;
      }
      case BoardPeripheral::kI2C: {
        auto value = backend->I2c(r);
        if (!value.ok()) {
          status = value.status();
          break;
        }
        auto wrapped = std::make_shared<I2c>(std::move(*value));
        group->endpoints_[r.config.name()] =
            std::static_pointer_cast<robot::comm::I2cTransport>(wrapped);
        group->closers_.push_back([wrapped] { return wrapped->Close(); });
        break;
      }
      case BoardPeripheral::kUart: {
        auto value = backend->Uart(r);
        if (!value.ok()) {
          status = value.status();
          break;
        }
        auto wrapped = std::make_shared<Uart>(std::move(*value));
        group->endpoints_[r.config.name()] =
            std::static_pointer_cast<robot::comm::ByteStream>(wrapped);
        group->closers_.push_back([wrapped] { return wrapped->Close(); });
        break;
      }
      default:
        status = absl::InvalidArgumentError("Unknown peripheral operation");
        break;
    }
    if (!status.ok()) {
      const auto cleanup = group->Close();
      return absl::Status(
          status.code(),
          "Peripheral '" + r.config.name() + "' (" + r.config.purpose() +
              "): " + std::string(status.message()) +
              (cleanup.ok() ? "" : "; rollback: " + std::string(cleanup.message())));
    }
  }
  return group;
}
absl::Status HostPeripherals::Close() {
  std::lock_guard lock(mutex_);
  if (closed_) return close_status_;
  closed_ = true;
  for (auto& stop : stoppers_) close_status_.Update(stop());
  for (auto it = closers_.rbegin(); it != closers_.rend(); ++it) close_status_.Update((*it)());
  return close_status_;
}
}  // namespace robot::board
