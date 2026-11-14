#include "robot/board/host_gpio/peripheral_backend.h"

#include <glob.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <utility>

#include "robot/comm/factory/comm_factory.h"
#include "utils/status_macros.h"
namespace robot::board {
namespace {
class NativeSystem final : public PeripheralSystem {
 public:
  std::vector<std::string> PwmChips() override {
    std::vector<std::string> result;
    glob_t g{};
    if (glob("/sys/class/pwm/pwmchip*", 0, nullptr, &g) == 0)
      for (size_t i = 0; i < g.gl_pathc; ++i) result.emplace_back(g.gl_pathv[i]);
    globfree(&g);
    return result;
  }
  absl::StatusOr<std::string> Read(const std::string& path) override {
    std::ifstream file(path);
    if (!file) return absl::UnavailableError("Cannot read " + path);
    return std::string(std::istreambuf_iterator<char>(file), {});
  }
  absl::Status Write(const std::string& path, const std::string& value) override {
    std::ofstream file(path);
    if (!file) return absl::UnavailableError("Cannot open " + path);
    file << value;
    file.flush();
    if (!file) return absl::UnavailableError("Write failed: " + path);
    return absl::OkStatus();
  }
  bool Exists(const std::string& path) override {
    std::error_code e;
    return std::filesystem::exists(path, e);
  }
  absl::StatusOr<std::string> Controller(const std::string& path, bool character) override {
    std::string base = path;
    if (character) {
      struct stat info {};
      if (stat(path.c_str(), &info) || !S_ISCHR(info.st_mode))
        return absl::InvalidArgumentError("UART selector must name a character device");
      base = "/sys/dev/char/" + std::to_string(major(info.st_rdev)) + ":" +
             std::to_string(minor(info.st_rdev));
    }
    std::error_code error;
    const auto node = std::filesystem::canonical(base + "/device/of_node", error);
    if (error)
      return absl::FailedPreconditionError("Peripheral has no device-tree identity: " + path);
    return node.filename().string();
  }
};
class Pwm final : public PwmOutput {
 public:
  Pwm(std::shared_ptr<PeripheralSystem> system, std::string chip, uint32_t channel, uint64_t period)
      : system_(std::move(system)),
        chip_(std::move(chip)),
        channel_(channel),
        period_(period),
        path_(chip_ + "/pwm" + std::to_string(channel)) {}
  ~Pwm() override {
    Close().IgnoreError();
  }
  absl::Status SetDutyCycle(double duty) override {
    if (!std::isfinite(duty) || duty < 0 || duty > 1)
      return absl::InvalidArgumentError("PWM duty must be finite in [0,1]");
    std::lock_guard lock(mutex_);
    if (closed_ || fault_) return absl::FailedPreconditionError("PWM is closed/faulted");
    auto status = system_->Write(
        path_ + "/duty_cycle", std::to_string(static_cast<uint64_t>(std::llround(duty * period_))));
    if (!status.ok()) {
      fault_ = true;
      system_->Write(path_ + "/enable", "0").IgnoreError();
    }
    return status;
  }
  absl::Status Enable() override {
    std::lock_guard lock(mutex_);
    if (closed_ || fault_) return absl::FailedPreconditionError("PWM is closed/faulted");
    auto status = system_->Write(path_ + "/enable", "1");
    if (!status.ok()) {
      fault_ = true;
      system_->Write(path_ + "/enable", "0").IgnoreError();
    }
    return status;
  }
  absl::Status Disable() override {
    std::lock_guard lock(mutex_);
    if (closed_) return close_status_;
    auto status = system_->Write(path_ + "/enable", "0");
    if (!status.ok()) fault_ = true;
    return status;
  }
  absl::Status Close() override {
    std::lock_guard lock(mutex_);
    if (closed_) return close_status_;
    closed_ = true;
    close_status_.Update(system_->Write(path_ + "/enable", "0"));
    close_status_.Update(system_->Write(chip_ + "/unexport", std::to_string(channel_)));
    return close_status_;
  }

 private:
  std::shared_ptr<PeripheralSystem> system_;
  std::string chip_;
  uint32_t channel_;
  uint64_t period_;
  std::string path_;
  std::mutex mutex_;
  bool closed_ = false, fault_ = false;
  absl::Status close_status_;
};
class LinuxBackend final : public PeripheralBackend {
 public:
  explicit LinuxBackend(std::shared_ptr<PeripheralSystem> system) : system_(std::move(system)) {}
  absl::StatusOr<std::shared_ptr<PwmOutput>> Pwm(const PeripheralResource& r) override {
    const auto& map = *r.pins[0].pwm;
    std::string chip;
    const auto expected = map.controller == "rp1_pwm0"
                              ? "pwm@98000"
                              : "pwm@" + map.controller.substr(0, map.controller.find('.'));
    for (const auto& candidate : system_->PwmChips()) {
      ABSL_ASSIGN_OR_RETURN(auto actual, system_->Controller(candidate, false));
      if (actual != expected) continue;
      if (!chip.empty()) return absl::FailedPreconditionError("PWM controller is ambiguous");
      chip = candidate;
    }
    if (chip.empty())
      return absl::NotFoundError("PWM controller not found; enable its platform overlay");
    ABSL_ASSIGN_OR_RETURN(auto count, system_->Read(chip + "/npwm"));
    size_t end = 0;
    uint64_t channels;
    try {
      channels = std::stoull(count, &end);
    } catch (...) {
      return absl::DataLossError("Invalid PWM channel count");
    }
    if (!channels || count.empty() || count[0] == '-' ||
        count.find_first_not_of(" \t\r\n", end) != std::string::npos)
      return absl::DataLossError("Invalid PWM channel count");
    if (map.channel >= channels)
      return absl::FailedPreconditionError("PWM channel exceeds kernel controller");
    const auto channel = std::to_string(map.channel);
    const auto path = chip + "/pwm" + channel;
    if (system_->Exists(path))
      return absl::AlreadyExistsError("PWM channel already exported; refusing to take ownership");
    ABSL_RETURN_IF_ERROR(system_->Write(chip + "/export", channel));
    const uint64_t period = 1000000000ull / r.config.pwm().frequency_hz();
    auto output = std::make_shared<robot::board::Pwm>(system_, chip, map.channel, period);
    // Export creates a disabled channel; initialize duty to zero before changing
    // period, then apply the configured duty while it remains disabled.
    auto status = system_->Write(path + "/enable", "0");
    if (status.ok()) status = system_->Write(path + "/duty_cycle", "0");
    if (status.ok()) status = system_->Write(path + "/period", std::to_string(period));
    if (status.ok()) status = output->SetDutyCycle(r.config.pwm().initial_duty_cycle());
    if (!status.ok()) {
      auto cleanup = output->Close();
      return absl::Status(
          status.code(),
          std::string(status.message()) +
              (cleanup.ok() ? "" : "; PWM rollback failed: " + std::string(cleanup.message())));
    }
    return std::static_pointer_cast<PwmOutput>(output);
  }
  absl::StatusOr<std::shared_ptr<robot::comm::SpiTransport>> Spi(
      const PeripheralResource& r) override {
    robot::comm::Comm c;
    c.set_comm_type(robot::comm::SPI);
    c.set_transport_type(robot::comm::SPI_TRANSFER);
    *c.mutable_spi_config() = r.config.spi().settings();
    ABSL_ASSIGN_OR_RETURN(auto transport, robot::comm::CommFactory::CreateComm(c));
    return robot::comm::GetCommTransport<robot::comm::SpiTransport>(transport);
  }
  absl::StatusOr<std::shared_ptr<robot::comm::I2cTransport>> I2c(
      const PeripheralResource& r) override {
    robot::comm::Comm c;
    c.set_comm_type(robot::comm::I2C);
    c.set_transport_type(robot::comm::I2C_TRANSFER);
    *c.mutable_i2c_config() = r.config.i2c().settings();
    ABSL_ASSIGN_OR_RETURN(auto transport, robot::comm::CommFactory::CreateComm(c));
    return robot::comm::GetCommTransport<robot::comm::I2cTransport>(transport);
  }
  absl::StatusOr<std::shared_ptr<robot::comm::ByteStream>> Uart(
      const PeripheralResource& r) override {
    const auto& settings = r.config.uart().settings();
    ABSL_ASSIGN_OR_RETURN(auto actual, system_->Controller(settings.port(), true));
    if (actual != r.config.uart().controller())
      return absl::FailedPreconditionError("UART controller identity mismatch");
    robot::comm::Comm c;
    c.set_comm_type(robot::comm::SERIAL);
    c.set_transport_type(robot::comm::BYTE_STREAM);
    *c.mutable_serial_config() = settings;
    ABSL_ASSIGN_OR_RETURN(auto transport, robot::comm::CommFactory::CreateComm(c));
    return robot::comm::GetCommTransport<robot::comm::ByteStream>(transport);
  }

 private:
  std::shared_ptr<PeripheralSystem> system_;
};
}  // namespace
std::shared_ptr<PeripheralBackend> MakeLinuxPeripheralBackend(
    std::shared_ptr<PeripheralSystem> system) {
  return std::make_shared<LinuxBackend>(system ? std::move(system)
                                               : std::make_shared<NativeSystem>());
}
}  // namespace robot::board
