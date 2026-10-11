#include "utils/robot_time.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "utils/robot_time_internal.h"

namespace joshua {
namespace {

struct GlobalClockState {
  std::once_flag once;
  std::unique_ptr<RobotClock> clock;
};

GlobalClockState& GlobalState() {
  // Intentionally process-lived: callbacks and static destructors may still
  // read the clock during shutdown. The OS reclaims the PHC fd at process exit.
  static auto* state = new GlobalClockState;
  return *state;
}

class SystemClockReader final : public internal::ClockIo {
 public:
  absl::StatusOr<int> OpenPtp(const std::string& path) override {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return absl::ErrnoToStatus(errno, absl::StrCat("Cannot open PTP clock ", path));
    return fd;
  }

  absl::StatusOr<timespec> Read(clockid_t id) const override {
    timespec ts{};
    if (clock_gettime(id, &ts) != 0) {
      return absl::ErrnoToStatus(errno, "Cannot read robot clock");
    }
    return ts;
  }

  void Close(int fd) override {
    close(fd);
  }
};

double ReadSeconds(const internal::ClockIo& reader, clockid_t id, int utc_offset = 0) {
  const auto result = reader.Read(id);
  if (!result.ok()) throw std::runtime_error(result.status().ToString());
  const auto& ts = *result;
  return (static_cast<double>(ts.tv_sec) - utc_offset - kRobotStartTime) +
         static_cast<double>(ts.tv_nsec) / kNanosecondsPerSecond;
}

// Owns a PHC descriptor and normalizes its readings to the UTC time domain.
class PtpClock final : public RobotClock {
 public:
  PtpClock(std::shared_ptr<internal::ClockIo> reader, int fd, int utc_offset)
      : reader_(std::move(reader)),
        fd_(fd),
        id_(internal::PtpClockId(fd)),
        utc_offset_(utc_offset) {}

  ~PtpClock() override {
    reader_->Close(fd_);
  }

  PtpClock(const PtpClock&) = delete;
  PtpClock& operator=(const PtpClock&) = delete;

  double Now() const override {
    return ReadSeconds(*reader_, id_, utc_offset_);
  }

  std::string_view SourceName() const override {
    return "PTP";
  }

 private:
  const std::shared_ptr<internal::ClockIo> reader_;
  const int fd_;
  const clockid_t id_;
  const int utc_offset_;
};

class UtcClock final : public RobotClock {
 public:
  explicit UtcClock(std::shared_ptr<internal::ClockIo> reader) : reader_(std::move(reader)) {}

  double Now() const override {
    return ReadSeconds(*reader_, CLOCK_REALTIME);
  }

  std::string_view SourceName() const override {
    return "UTC";
  }

 private:
  const std::shared_ptr<internal::ClockIo> reader_;
};

// Explicit local-time option; never selected as an automatic UTC fallback.
class MonotonicClock final : public RobotClock {
 public:
  explicit MonotonicClock(std::shared_ptr<internal::ClockIo> reader) : reader_(std::move(reader)) {}

  double Now() const override {
    return ReadSeconds(*reader_, CLOCK_MONOTONIC);
  }

  std::string_view SourceName() const override {
    return "MONOTONIC (local only)";
  }

 private:
  const std::shared_ptr<internal::ClockIo> reader_;
};

absl::StatusOr<std::unique_ptr<RobotClock>> ProbeClock(std::unique_ptr<RobotClock> clock,
                                                       const internal::ClockIo& reader,
                                                       clockid_t id) {
  const auto result = reader.Read(id);
  if (!result.ok()) return result.status();  // RAII closes any PHC descriptor.
  return clock;
}

}  // namespace

absl::Status ValidateRobotClockConfig(const config::RobotClockConfig& config) {
  if (!config::RobotClockConfig::Source_IsValid(config.source())) {
    return absl::InvalidArgumentError("Unknown robot clock source");
  }
  if (config.source() != config::RobotClockConfig::PTP &&
      (config.require_ptp() || !config.ptp_device().empty() ||
       config.has_ptp_utc_offset_seconds())) {
    return absl::InvalidArgumentError("PTP settings require robot clock source PTP");
  }
  if (config.ptp_device().find('\0') != std::string::npos) {
    return absl::InvalidArgumentError("PTP device path contains a null byte");
  }
  if (config.require_ptp() && !config.has_ptp_utc_offset_seconds()) {
    return absl::InvalidArgumentError("Required PTP needs an explicit ptp_utc_offset_seconds");
  }
  return absl::OkStatus();
}

namespace internal {

clockid_t PtpClockId(int fd) {
  // Linux FD_TO_CLOCKID, expressed with unsigned arithmetic to avoid shifting
  // a negative signed value. clockid_t is signed 32-bit on supported Linux ABIs.
  return static_cast<clockid_t>((~static_cast<uint32_t>(fd) << 3) | 3U);
}

absl::StatusOr<std::unique_ptr<RobotClock>> MakeRobotClockWithIo(
    const config::RobotClockConfig& config, std::shared_ptr<ClockIo> io) {
  const auto status = ValidateRobotClockConfig(config);
  if (!status.ok()) return status;
  if (!io) return absl::InvalidArgumentError("Clock IO must not be null");

  if (config.source() == config::RobotClockConfig::MONOTONIC) {
    return ProbeClock(std::make_unique<MonotonicClock>(io), *io, CLOCK_MONOTONIC);
  }
  if (config.source() == config::RobotClockConfig::PTP) {
    auto ptp_status = absl::FailedPreconditionError(
        "PTP UTC offset is unknown; configure ptp_utc_offset_seconds");
    if (config.has_ptp_utc_offset_seconds()) {
      const auto fd = io->OpenPtp(config.ptp_device().empty() ? "/dev/ptp0" : config.ptp_device());
      if (fd.ok()) {
        auto clock =
            ProbeClock(std::make_unique<PtpClock>(io, *fd, config.ptp_utc_offset_seconds()),
                       *io,
                       PtpClockId(*fd));
        if (clock.ok()) return clock;
        ptp_status = clock.status();
      } else {
        ptp_status = fd.status();
      }
    }
    if (config.require_ptp()) return ptp_status;
    LOG(WARNING) << "Robot clock falling back from PTP to system UTC: " << ptp_status;
  }
  return ProbeClock(std::make_unique<UtcClock>(io), *io, CLOCK_REALTIME);
}

}  // namespace internal

absl::StatusOr<std::unique_ptr<RobotClock>> MakeRobotClock(const config::RobotClockConfig& config) {
  return internal::MakeRobotClockWithIo(config, std::make_shared<SystemClockReader>());
}

absl::Status SetGlobalRobotClock(std::unique_ptr<RobotClock> clock) {
  if (!clock) return absl::InvalidArgumentError("Robot clock must not be null");
  auto& state = GlobalState();
  bool installed = false;
  std::call_once(state.once, [&] {
    state.clock = std::move(clock);
    installed = true;
  });
  if (!installed) {
    return absl::FailedPreconditionError(
        "Robot clock already initialized; install before the first RobotTime() call");
  }
  return absl::OkStatus();
}

void CheckGlobalClock() {
  auto& state = GlobalState();
  std::call_once(state.once, [&] {
    auto clock = MakeRobotClock(config::RobotClockConfig{});
    if (!clock.ok()) throw std::runtime_error(clock.status().ToString());
    state.clock = std::move(*clock);
  });
}

double RobotTime() {
  CheckGlobalClock();
  return GlobalState().clock->Now();
}

}  // namespace joshua
