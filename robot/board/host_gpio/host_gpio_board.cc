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

HostGpioBoard::HostGpioBoard() : HostGpioBoard(std::make_shared<LinuxGpioBackend>()) {}
HostGpioBoard::HostGpioBoard(std::shared_ptr<GpioBackend> backend) : backend_(std::move(backend)) {}
HostGpioBoard::~HostGpioBoard() {
  Teardown().IgnoreError();
}

absl::Status HostGpioBoard::Init(const Board& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (used_) return absl::FailedPreconditionError("GPIO board lifecycle already started");
  ABSL_ASSIGN_OR_RETURN(auto resources, ResolveHostGpioPins(config));
  ABSL_ASSIGN_OR_RETURN(const auto* profile,
                        FindPinProfile(config.host_gpio_config().profile_id(),
                                       config.host_gpio_config().profile_version()));
  if (!backend_) return absl::InvalidArgumentError("GPIO backend is null");
  ABSL_RETURN_IF_ERROR(backend_->CheckPlatform(*profile));
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
  initialized_ = false;
  // Keep closed pins and their faults until destruction; repeated cleanup reports
  // the original failure instead of converting an uncertain shutdown to success.
  return result;
}

absl::Status HostGpioBoard::Teardown() {
  std::lock_guard<std::mutex> lock(mutex_);
  return ClosePins();
}

absl::Status HostGpioBoard::EmergencyStop() {
  return Teardown();
}
}  // namespace robot::board
