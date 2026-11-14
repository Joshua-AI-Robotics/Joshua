#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <variant>

#include "robot/board/host_gpio/peripheral_backend.h"
namespace robot::board {
class HostPeripherals {
 public:
  using Endpoint = std::variant<std::shared_ptr<PwmOutput>,
                                std::shared_ptr<robot::comm::SpiTransport>,
                                std::shared_ptr<robot::comm::I2cTransport>,
                                std::shared_ptr<robot::comm::ByteStream>>;
  ~HostPeripherals();
  static absl::StatusOr<std::shared_ptr<HostPeripherals>> Open(const Board&,
                                                               std::shared_ptr<PeripheralBackend>);
  template <class T>
  absl::StatusOr<std::shared_ptr<T>> Get(const std::string& name) {
    std::lock_guard lock(mutex_);
    if (closed_) return absl::FailedPreconditionError("Board peripherals are closed");
    const auto it = endpoints_.find(name);
    if (it == endpoints_.end()) return absl::NotFoundError("Undeclared peripheral: " + name);
    const auto* endpoint = std::get_if<std::shared_ptr<T>>(&it->second);
    if (!endpoint)
      return absl::InvalidArgumentError("Peripheral type differs from requested capability");
    return *endpoint;
  }
  absl::Status Close();

 private:
  std::mutex mutex_;
  bool closed_ = false;
  absl::Status close_status_;
  std::map<std::string, Endpoint> endpoints_;
  std::vector<std::function<absl::Status()>> closers_;
  std::vector<std::function<absl::Status()>> stoppers_;
};
}  // namespace robot::board
