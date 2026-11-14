#include "robot/board/joshua_wire/peripheral_backend.h"

#include <cmath>
#include <mutex>

#include "utils/status_macros.h"
namespace robot::board {
namespace {
struct Endpoint {
  PeripheralExchange exchange;
  uint8_t id, kind;
  absl::StatusOr<std::vector<uint8_t>> Call(uint8_t op, const std::vector<uint8_t>& data = {}) {
    return exchange(id, op, data);
  }
  absl::Status Configure(const PeripheralResource& r) {
    ABSL_ASSIGN_OR_RETURN(auto c, PeripheralConfig(r));
    uint8_t bytes[JW_PERIPHERAL_CONFIG_LEN];
    const int n = jw_encode_peripheral_config(bytes, sizeof(bytes), &c);
    return Call(JW_PERIPHERAL_CONFIGURE, {bytes, bytes + n}).status();
  }
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& w, size_t n) {
    if (w.size() > 32 || n > 32 ||
        (kind == JW_PERIPHERAL_SPI ? w.empty() || n != w.size()
                                   : w.size() + n == 0 || w.size() + n > 32 ||
                                         (kind == JW_PERIPHERAL_UART && !w.empty() && n)))
      return absl::InvalidArgumentError("Invalid bounded peripheral transfer");
    std::vector<uint8_t> q{kind, static_cast<uint8_t>(w.size()), static_cast<uint8_t>(n)};
    q.insert(q.end(), w.begin(), w.end());
    ABSL_ASSIGN_OR_RETURN(auto r, Call(JW_PERIPHERAL_TRANSFER, q));
    // Session already validates success semantics, including the exact count.
    if (r.empty() || r.size() != r[0] + 1u)
      return absl::DataLossError("Malformed peripheral result");
    return std::vector<uint8_t>(r.begin() + 1, r.end());
  }
};
class Pwm final : public PwmOutput {
 public:
  explicit Pwm(Endpoint e) : e_(std::move(e)) {}
  absl::Status SetDutyCycle(double v) override {
    if (!std::isfinite(v) || v < 0 || v > 1)
      return absl::InvalidArgumentError("Duty must be finite in [0,1]");
    const auto value = static_cast<uint16_t>(std::lround(v * 65535));
    return Call(JW_PERIPHERAL_WRITE,
                {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)});
  }
  absl::Status Enable() override {
    return Call(JW_PERIPHERAL_ENABLE);
  }
  absl::Status Disable() override {
    return Call(JW_PERIPHERAL_DISABLE);
  }
  absl::Status Close() override {
    std::lock_guard lock(mutex_);
    if (closed_) return status_;
    closed_ = true;
    status_ = e_.Call(JW_PERIPHERAL_RELEASE).status();
    return status_;
  }

 private:
  absl::Status Call(uint8_t op, const std::vector<uint8_t>& d = {}) {
    std::lock_guard lock(mutex_);
    if (closed_) return absl::FailedPreconditionError("PWM closed");
    return e_.Call(op, d).status();
  }
  Endpoint e_;
  std::mutex mutex_;
  bool closed_ = false;
  absl::Status status_;
};
class Spi final : public robot::comm::SpiTransport {
 public:
  explicit Spi(Endpoint e) : e_(std::move(e)) {}
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& w) override {
    return e_.Transfer(w, w.size());
  }

 private:
  Endpoint e_;
};
class I2c final : public robot::comm::I2cTransport {
 public:
  explicit I2c(Endpoint e) : e_(std::move(e)) {}
  absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& w, size_t n) override {
    return e_.Transfer(w, n);
  }

 private:
  Endpoint e_;
};
class Uart final : public robot::comm::ByteStream {
 public:
  explicit Uart(Endpoint e) : e_(std::move(e)) {}
  absl::Status Open() override {
    return absl::OkStatus();
  }  // Managed gate supplies terminal lifetime.
  absl::Status Write(const std::vector<uint8_t>& w) override {
    return e_.Transfer(w, 0).status();
  }
  absl::StatusOr<std::vector<uint8_t>> Read(size_t n) override {
    return e_.Transfer({}, n);
  }

 private:
  Endpoint e_;
};
class Digital final : public DigitalPin {
 public:
  Digital(Endpoint e, PinResource r) : e_(std::move(e)), resource_(std::move(r)) {}
  const PinResource& resource() const override {
    return resource_;
  }
  absl::StatusOr<bool> Read() override {
    ABSL_ASSIGN_OR_RETURN(auto r, e_.Call(JW_PERIPHERAL_READ));
    if (r.size() != 1 || r[0] > 1) return absl::DataLossError("Malformed digital value");
    return r[0] != 0;
  }
  absl::Status Write(bool v) override {
    if (resource_.config.direction() != DIGITAL_OUTPUT)
      return absl::FailedPreconditionError("Digital input is read only");
    return e_.Call(JW_PERIPHERAL_WRITE, {static_cast<uint8_t>(v)}).status();
  }

 private:
  Endpoint e_;
  PinResource resource_;
};
class Backend final : public PeripheralBackend {
 public:
  explicit Backend(PeripheralExchange e) : exchange_(std::move(e)) {}
  absl::StatusOr<std::shared_ptr<PwmOutput>> Pwm(const PeripheralResource& r) override {
    ABSL_ASSIGN_OR_RETURN(auto e, Open(r, JW_PERIPHERAL_PWM));
    return std::make_shared<robot::board::Pwm>(std::move(e));
  }
  absl::StatusOr<std::shared_ptr<robot::comm::SpiTransport>> Spi(
      const PeripheralResource& r) override {
    ABSL_ASSIGN_OR_RETURN(auto e, Open(r, JW_PERIPHERAL_SPI));
    return std::make_shared<robot::board::Spi>(std::move(e));
  }
  absl::StatusOr<std::shared_ptr<robot::comm::I2cTransport>> I2c(
      const PeripheralResource& r) override {
    ABSL_ASSIGN_OR_RETURN(auto e, Open(r, JW_PERIPHERAL_I2C));
    return std::make_shared<robot::board::I2c>(std::move(e));
  }
  absl::StatusOr<std::shared_ptr<robot::comm::ByteStream>> Uart(
      const PeripheralResource& r) override {
    ABSL_ASSIGN_OR_RETURN(auto e, Open(r, JW_PERIPHERAL_UART));
    return std::make_shared<robot::board::Uart>(std::move(e));
  }

 private:
  absl::StatusOr<Endpoint> Open(const PeripheralResource& r, uint8_t kind) {
    Endpoint e{exchange_, static_cast<uint8_t>(r.config.function_id()), kind};
    ABSL_RETURN_IF_ERROR(e.Configure(r));
    return e;
  }
  PeripheralExchange exchange_;
};
}  // namespace
std::shared_ptr<PeripheralBackend> MakeJoshuaWirePeripheralBackend(PeripheralExchange e) {
  return std::make_shared<Backend>(std::move(e));
}
absl::StatusOr<std::shared_ptr<DigitalPin>> MakeJoshuaWireDigitalPin(const PeripheralResource& r,
                                                                     const PinProfile& p,
                                                                     PeripheralExchange exchange) {
  Endpoint e{
      std::move(exchange), static_cast<uint8_t>(r.config.function_id()), JW_PERIPHERAL_DIGITAL};
  ABSL_ASSIGN_OR_RETURN(auto pin, ResolvePin(p, r.config.digital()));
  ABSL_RETURN_IF_ERROR(e.Configure(r));
  return std::make_shared<Digital>(std::move(e), std::move(pin));
}
}  // namespace robot::board
