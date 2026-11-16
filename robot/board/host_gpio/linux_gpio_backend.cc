#include "robot/board/host_gpio/linux_gpio_backend.h"

#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#include "absl/strings/str_cat.h"

namespace robot::board {
namespace {
class LinuxSystem final : public GpioSystem {
 public:
  std::vector<std::string> Chips() override {
    glob_t paths{};
    std::vector<std::string> result;
    if (glob("/dev/gpiochip*", 0, nullptr, &paths) == 0)
      for (size_t i = 0; i < paths.gl_pathc; ++i) result.emplace_back(paths.gl_pathv[i]);
    globfree(&paths);
    return result;
  }
  std::string Compatible() override {
    std::ifstream input("/proc/device-tree/compatible", std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  int Open(const std::string& path) override {
    return open(path.c_str(), O_RDONLY | O_CLOEXEC);
  }
  int Ioctl(int fd, unsigned long operation, void* argument) override {
    int result;
    do {
      result = ioctl(fd, operation, argument);
    } while (result < 0 && errno == EINTR);
    return result;
  }
  void Close(int fd) override {
    close(fd);
  }
};

absl::Status Error(const std::string& context) {
  const int error = errno;
  auto code = absl::StatusCode::kUnavailable;
  if (error == EBUSY) code = absl::StatusCode::kAlreadyExists;
  if (error == EACCES || error == EPERM) code = absl::StatusCode::kPermissionDenied;
  if (error == ENOTTY || error == EOPNOTSUPP) code = absl::StatusCode::kUnimplemented;
  return absl::Status(
      code,
      absl::StrCat(
          context,
          ": ",
          std::strerror(error),
          error == ENOTTY
              ? " (GPIO character-device ABI v2 requires host kernel >= 5.10 and driver support)"
              : ""));
}

class Fd {
 public:
  Fd(std::shared_ptr<GpioSystem> system, int fd) : system_(std::move(system)), fd_(fd) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() {
    if (fd_ >= 0) system_->Close(fd_);
  }
  int get() const {
    return fd_;
  }

 private:
  std::shared_ptr<GpioSystem> system_;
  int fd_;
};

class LinuxLine final : public GpioLine {
 public:
  LinuxLine(std::shared_ptr<GpioSystem> system, int fd, std::string context, bool output)
      : system_(std::move(system)),
        fd_(system_, fd),
        context_(std::move(context)),
        output_(output) {}
  absl::StatusOr<bool> Read() override {
    gpio_v2_line_values values{};
    values.mask = 1;
    if (system_->Ioctl(fd_.get(), GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0)
      return Error(context_);
    return (values.bits & 1) != 0;
  }
  absl::Status Write(bool active) override {
    if (!output_) return absl::FailedPreconditionError("Cannot write GPIO input");
    gpio_v2_line_values values{};
    values.mask = 1;
    values.bits = active ? 1 : 0;
    if (system_->Ioctl(fd_.get(), GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0)
      return Error(context_);
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<GpioSystem> system_;
  Fd fd_;
  std::string context_;
  bool output_;
};
}  // namespace

LinuxGpioBackend::LinuxGpioBackend() : LinuxGpioBackend(std::make_shared<LinuxSystem>()) {}
LinuxGpioBackend::LinuxGpioBackend(std::shared_ptr<GpioSystem> system)
    : system_(std::move(system)) {}

absl::Status LinuxGpioBackend::CheckPlatform(const PinProfile& profile) {
  const auto compatible = system_->Compatible();
  size_t start = 0;
  while (start < compatible.size()) {
    const auto end = compatible.find('\0', start);
    const auto token = compatible.substr(start, end == std::string::npos ? end : end - start);
    for (const auto& expected : profile.compatible)
      if (token == expected) return absl::OkStatus();
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return absl::FailedPreconditionError("Device-tree platform/carrier does not match GPIO profile " +
                                       profile.id);
}

absl::StatusOr<std::unique_ptr<GpioLine>> LinuxGpioBackend::Request(const PinResource& pin) {
  std::string selected;
  uint32_t offset = 0;
  // Complete discovery before acquisition. Neither chip numbering nor global
  // Linux GPIO numbers identify a connector; require unique controller + name.
  for (const auto& path : system_->Chips()) {
    const int opened = system_->Open(path);
    if (opened < 0) return Error("Inspect GPIO chip " + path);
    Fd chip(system_, opened);
    gpiochip_info info{};
    if (system_->Ioctl(chip.get(), GPIO_GET_CHIPINFO_IOCTL, &info) < 0) return Error(path);
    if (pin.info.chip_label != info.label) continue;
    for (uint32_t i = 0; i < info.lines; ++i) {
      gpio_v2_line_info line{};
      line.offset = i;
      if (system_->Ioctl(chip.get(), GPIO_V2_GET_LINEINFO_IOCTL, &line) < 0) return Error(path);
      if (pin.info.line_name != line.name) continue;
      if (!selected.empty())
        return absl::FailedPreconditionError("Ambiguous GPIO controller/line " + pin.identity());
      if (line.flags & GPIO_V2_LINE_FLAG_USED)
        return absl::AlreadyExistsError("GPIO line already claimed: " + pin.identity());
      selected = path;
      offset = i;
    }
  }
  if (selected.empty())
    return absl::NotFoundError("GPIO controller/line not found: " + pin.identity());
  const int opened = system_->Open(selected);
  if (opened < 0) return Error(selected);
  Fd chip(system_, opened);
  // Recheck identity on the actual descriptor used for the exclusive request.
  gpiochip_info info{};
  gpio_v2_line_info line{};
  line.offset = offset;
  if (system_->Ioctl(chip.get(), GPIO_GET_CHIPINFO_IOCTL, &info) < 0 ||
      system_->Ioctl(chip.get(), GPIO_V2_GET_LINEINFO_IOCTL, &line) < 0)
    return Error(selected);
  if (pin.info.chip_label != info.label || pin.info.line_name != line.name)
    return absl::FailedPreconditionError("GPIO identity changed during acquisition");
  gpio_v2_line_request request{};
  request.num_lines = 1;
  request.offsets[0] = offset;
  std::snprintf(request.consumer, sizeof(request.consumer), "joshua:%s", pin.config.name().c_str());
  const bool output = pin.config.direction() == DIGITAL_OUTPUT;
  request.config.flags = output ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
  if (pin.config.active_low()) request.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
  switch (pin.config.bias()) {
    case DIGITAL_BIAS_DISABLED:
      request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
      break;
    case DIGITAL_BIAS_PULL_UP:
      request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
      break;
    case DIGITAL_BIAS_PULL_DOWN:
      request.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
      break;
    default:
      break;
  }
  if (output) {
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = pin.config.initial_value() ? 1 : 0;
    request.config.attrs[0].mask = 1;
  }
  if (system_->Ioctl(chip.get(), GPIO_V2_GET_LINE_IOCTL, &request) < 0)
    return Error(selected + " " + pin.identity());
  return std::unique_ptr<GpioLine>(
      new LinuxLine(system_, request.fd, selected + " " + pin.identity(), output));
}
}  // namespace robot::board
