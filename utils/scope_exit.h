#pragma once

#include <utility>

namespace joshua {

// The callback must not throw. Used for state restoration during unwinding.
template <typename Callback>
class ScopeExit {
 public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() noexcept { callback_(); }
  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

 private:
  Callback callback_;
};

}  // namespace joshua
