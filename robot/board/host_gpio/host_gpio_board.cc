#include "robot/board/host_gpio/host_gpio_board.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "robot/board/host_gpio/linux_gpio_backend.h"
#include "utils/status_macros.h"

namespace robot::board {

class HostGpioBoard::Pin final : public DigitalPin {
 public:
  Pin(PinResource resource, std::unique_ptr<GpioLine> line)
      : resource_(std::move(resource)), line_(std::move(line)) {}
  const PinResource& resource() const override {
    return resource_;
  }
  absl::StatusOr<bool> Read() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!line_) return absl::FailedPreconditionError("GPIO pin is closed");
    auto value = line_->Read();
    if (!value.ok()) return Fault(value.status());
    return *value;
  }
  absl::Status Write(bool active) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!line_) return absl::FailedPreconditionError("GPIO pin is closed");
    if (resource_.config.direction() != DIGITAL_OUTPUT)
      return absl::FailedPreconditionError("Cannot write to digital input");
    auto status = line_->Write(active);
    if (!status.ok()) return Fault(status);
    return status;
  }
  absl::Status Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!line_) return fault_;
    absl::Status result;
    if (resource_.config.direction() == DIGITAL_OUTPUT)
      result = Context(line_->Write(resource_.config.shutdown_value()));
    line_.reset();
    if (!result.ok()) fault_ = result;
    return fault_;
  }

 private:
  absl::Status Context(const absl::Status& status) const {
    if (status.ok()) return status;
    return absl::Status(status.code(),
                        absl::StrCat("GPIO '",
                                     resource_.config.name(),
                                     "' (",
                                     resource_.config.purpose(),
                                     ", ",
                                     resource_.identity(),
                                     "): ",
                                     status.message()));
  }
  absl::Status Fault(const absl::Status& status) {
    fault_ = Context(status);
    if (resource_.config.direction() == DIGITAL_OUTPUT)
      line_->Write(resource_.config.shutdown_value()).IgnoreError();
    line_.reset();
    return fault_;
  }
  const PinResource resource_;
  std::mutex mutex_;
  std::unique_ptr<GpioLine> line_;
  absl::Status fault_;
};

HostGpioBoard::HostGpioBoard() : HostGpioBoard(std::make_shared<LinuxGpioBackend>()) {
  reserve_resources_ = true;
}
HostGpioBoard::HostGpioBoard(std::shared_ptr<GpioBackend> backend,
                             std::shared_ptr<PeripheralBackend> peripherals)
    : backend_(std::move(backend)),
      peripheral_backend_(peripherals ? std::move(peripherals) : MakeLinuxPeripheralBackend()) {}
HostGpioBoard::~HostGpioBoard() {
  Teardown().IgnoreError();
}

absl::Status HostGpioBoard::Init(const Board& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (used_) return absl::FailedPreconditionError("GPIO board lifecycle already started");
  ABSL_ASSIGN_OR_RETURN(auto resources, ResolveHostGpioPins(config));
  ABSL_RETURN_IF_ERROR(ResolveBoardPeripherals(config).status());
  ABSL_ASSIGN_OR_RETURN(const auto* profile,
                        FindPinProfile(config.host_gpio_config().profile_id(),
                                       config.host_gpio_config().profile_version()));
  if (!backend_) return absl::InvalidArgumentError("GPIO backend is null");
  ABSL_RETURN_IF_ERROR(backend_->CheckPlatform(*profile));
  if (reserve_resources_) {
    std::vector<std::string> claims;
    for (const auto& p : resources) claims.push_back(PinIdentity(*profile, p.info));
    ABSL_ASSIGN_OR_RETURN(auto peripherals, ResolveBoardPeripherals(config));
    for (const auto& p : peripherals)
      for (const auto& claim : p.claims) claims.push_back(claim.identity);
    const auto& configured = config.host_gpio_config().lease_directory();
    ABSL_ASSIGN_OR_RETURN(
        reservations_,
        ResourceLeases::Acquire(std::move(claims),
                                configured.empty() ? "/run/lock/joshua" : configured));
  }
  used_ = true;
  for (auto& resource : resources) {
    auto request = backend_->Request(resource);
    if (!request.ok()) {
      const auto rollback = ClosePins();
      return absl::Status(
          request.status().code(),
          absl::StrCat("Board '",
                       config.name(),
                       "' pin '",
                       resource.config.name(),
                       "' (",
                       resource.config.purpose(),
                       "): ",
                       request.status().message(),
                       rollback.ok() ? "" : absl::StrCat("; rollback: ", rollback.message())));
    }
    auto pin = std::make_shared<Pin>(std::move(resource), std::move(*request));
    pins_.emplace(pin->resource().config.name(), pin);
    acquisition_order_.push_back(std::move(pin));
  }
  auto group = HostPeripherals::Open(config, peripheral_backend_);
  if (!group.ok()) {
    const auto cleanup = ClosePins();
    return absl::Status(
        group.status().code(),
        std::string(group.status().message()) +
            (cleanup.ok() ? "" : "; GPIO rollback: " + std::string(cleanup.message())));
  }
  peripherals_ = std::move(*group);
  initialized_ = true;
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<BoardChannel>> HostGpioBoard::OpenChannel(uint32_t) {
  return absl::UnimplementedError(
      "HOST_GPIO supports digital pins; PWM and STEP/DIR need qualified backends");
}

absl::StatusOr<std::shared_ptr<DigitalPin>> HostGpioBoard::OpenDigitalPin(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return absl::FailedPreconditionError("GPIO board is not initialized");
  const auto it = pins_.find(name);
  if (it == pins_.end()) return absl::NotFoundError("Undeclared digital pin: " + name);
  return std::static_pointer_cast<DigitalPin>(it->second);
}

absl::Status HostGpioBoard::ClosePins() {
  absl::Status result;
  for (auto it = acquisition_order_.rbegin(); it != acquisition_order_.rend(); ++it)
    result.Update((*it)->Close());
  if (peripherals_) result.Update(peripherals_->Close());
  initialized_ = false;
  reservations_.reset();
  // Keep closed pins and their faults until destruction; repeated cleanup reports
  // the original failure instead of converting an uncertain shutdown to success.
  return result;
}

absl::StatusOr<std::shared_ptr<PwmOutput>> HostGpioBoard::OpenPwm(const std::string& name) {
  std::lock_guard lock(mutex_);
  if (!initialized_) return absl::FailedPreconditionError("Board is closed");
  return peripherals_->Get<PwmOutput>(name);
}
absl::StatusOr<std::shared_ptr<robot::comm::SpiTransport>> HostGpioBoard::OpenSpi(
    const std::string& name) {
  std::lock_guard lock(mutex_);
  if (!initialized_) return absl::FailedPreconditionError("Board is closed");
  return peripherals_->Get<robot::comm::SpiTransport>(name);
}
absl::StatusOr<std::shared_ptr<robot::comm::I2cTransport>> HostGpioBoard::OpenI2c(
    const std::string& name) {
  std::lock_guard lock(mutex_);
  if (!initialized_) return absl::FailedPreconditionError("Board is closed");
  return peripherals_->Get<robot::comm::I2cTransport>(name);
}
absl::StatusOr<std::shared_ptr<robot::comm::ByteStream>> HostGpioBoard::OpenUart(
    const std::string& name) {
  std::lock_guard lock(mutex_);
  if (!initialized_) return absl::FailedPreconditionError("Board is closed");
  return peripherals_->Get<robot::comm::ByteStream>(name);
}
absl::Status HostGpioBoard::Teardown() {
  std::lock_guard<std::mutex> lock(mutex_);
  return ClosePins();
}

absl::Status HostGpioBoard::EmergencyStop() {
  return Teardown();
}
}  // namespace robot::board
